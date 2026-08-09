/*
 * ArpPatternGrid.cpp — JT-8000 Arpeggiator tri-lane pattern editor (Phase 9)
 *
 * See ArpPatternGrid.h for the contract. Mirrors StepSequencerGrid's idioms:
 * click/drag editing, orange theme, playhead marker, wire-form values via the
 * onLaneEdit callback. Ratchet uses the same ValueLaw int-lattice the firmware
 * decodes (lroundf(norm*3)), so 1..4 round-trips exactly to bytes {0,42,85,127}.
 */

#include "ArpPatternGrid.h"
#include "ValueLaw.h"   // JUCE-free int-lattice law shared with the firmware

ArpPatternGrid::ArpPatternGrid()
{
    for (int i = 0; i < kNumSteps; ++i)
    {
        _on[i]      = true;    // fresh pattern plays every step
        _accent[i]  = 127;     // full accent
        _ratchet[i] = 1;       // single hit
    }
    setInterceptsMouseClicks(true, false);
}

// ---- ratchet lattice (matches firmware arp.step_ratchet decode) -------------
int ArpPatternGrid::ratchetToByte(int count)
{
    count = juce::jlimit(1, 4, count);
    return ValueLaw::ccFromIntLattice(count - 1, 0, 3);   // {0,42,85,127}
}
int ArpPatternGrid::byteToRatchet(int byte)
{
    return ValueLaw::intLatticeFromCc(juce::jlimit(0, 127, byte), 0, 3) + 1;
}

// ---- model setters ----------------------------------------------------------
void ArpPatternGrid::setOnOff(int step, bool on, bool notify)
{
    if (step < 0 || step >= kNumSteps) return;
    _on[step] = on;
    if (notify && onLaneEdit) onLaneEdit(step, Lane::OnOff, onOffToByte(on));
    repaint();
}
void ArpPatternGrid::setAccent(int step, uint8_t v, bool notify)
{
    if (step < 0 || step >= kNumSteps) return;
    _accent[step] = v;
    if (notify && onLaneEdit) onLaneEdit(step, Lane::Accent, (int)v);
    repaint();
}
void ArpPatternGrid::setRatchet(int step, int count, bool notify)
{
    if (step < 0 || step >= kNumSteps) return;
    _ratchet[step] = (uint8_t)juce::jlimit(1, 4, count);
    if (notify && onLaneEdit) onLaneEdit(step, Lane::Ratchet, ratchetToByte(_ratchet[step]));
    repaint();
}

bool    ArpPatternGrid::getOnOff  (int step) const { return (step >= 0 && step < kNumSteps) ? _on[step] : false; }
uint8_t ArpPatternGrid::getAccent (int step) const { return (step >= 0 && step < kNumSteps) ? _accent[step] : 0; }
int     ArpPatternGrid::getRatchet(int step) const { return (step >= 0 && step < kNumSteps) ? _ratchet[step] : 1; }

void ArpPatternGrid::sendAll()
{
    if (!onLaneEdit) return;
    for (int s = 0; s < kNumSteps; ++s)
    {
        onLaneEdit(s, Lane::OnOff,   onOffToByte(_on[s]));
        onLaneEdit(s, Lane::Accent,  (int)_accent[s]);
        onLaneEdit(s, Lane::Ratchet, ratchetToByte(_ratchet[s]));
    }
}

void ArpPatternGrid::setPlayhead(int step, bool running)
{
    const int next = running ? step : -1;
    if (next == _playStep) return;
    _playStep = next;
    repaint();   // whole-widget repaint is fine at 1/16 rates for three thin lanes
}

// ---- geometry ---------------------------------------------------------------
juce::Rectangle<int> ArpPatternGrid::laneArea(Lane lane) const
{
    auto r = getLocalBounds().reduced(0, kTopPad);
    r.removeFromBottom(kNumH);                 // step-number strip
    const int laneH = r.getHeight() / 3;
    const int idx = (int)lane;
    return { r.getX() + kLabelW, r.getY() + idx * laneH,
             r.getWidth() - kLabelW, laneH - kGap };
}

juce::Rectangle<float> ArpPatternGrid::cellArea(Lane lane, int step) const
{
    auto la = laneArea(lane).toFloat();
    const float cw = la.getWidth() / (float)kNumSteps;
    return { la.getX() + step * cw + 1.0f, la.getY(), cw - 2.0f, la.getHeight() };
}

int ArpPatternGrid::stepFromX(float x) const
{
    auto la = laneArea(Lane::OnOff).toFloat();
    if (la.getWidth() <= 0.0f) return -1;
    const int s = (int)((x - la.getX()) / (la.getWidth() / (float)kNumSteps));
    return juce::jlimit(0, kNumSteps - 1, s);
}

// ---- paint ------------------------------------------------------------------
void ArpPatternGrid::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101010));

    auto drawLaneLabel = [&](Lane lane, const juce::String& text)
    {
        auto la = laneArea(lane);
        g.setColour(juce::Colour(0xffb0b0b0));
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(text, 2, la.getY(), kLabelW - 4, la.getHeight(),
                   juce::Justification::centredLeft);
    };
    drawLaneLabel(Lane::OnOff,   "Gate");
    drawLaneLabel(Lane::Accent,  "Accent");
    drawLaneLabel(Lane::Ratchet, "Ratchet");

    for (int s = 0; s < kNumSteps; ++s)
    {
        const bool playing = (s == _playStep);

        // On/off: filled orange cell when on, dim outline when rest.
        {
            auto c = cellArea(Lane::OnOff, s);
            if (_on[s]) g.setColour(playing ? juce::Colours::white : _fill);
            else        g.setColour(_dim);
            g.fillRect(c.reduced(0.0f, c.getHeight() * 0.2f));
        }

        // Accent: bar rising from the lane bottom, height ∝ value.
        {
            auto c = cellArea(Lane::Accent, s);
            const float frac = _accent[s] / 127.0f;
            auto bar = c.withTop(c.getBottom() - c.getHeight() * frac);
            g.setColour(_dim);
            g.fillRect(c);
            g.setColour(playing ? juce::Colours::white : _accCol);
            g.fillRect(bar);
        }

        // Ratchet: 1..4 steps of height (quantised mini-bar).
        {
            auto c = cellArea(Lane::Ratchet, s);
            const float frac = (float)(_ratchet[s]) / 4.0f;   // 1..4 -> 0.25..1
            auto bar = c.withTop(c.getBottom() - c.getHeight() * frac);
            g.setColour(_dim);
            g.fillRect(c);
            g.setColour(playing ? juce::Colours::white : _ratCol);
            g.fillRect(bar);
            // draw the count digit
            g.setColour(juce::Colours::black);
            g.setFont(juce::FontOptions(9.0f));
            g.drawText(juce::String(_ratchet[s]), c.toNearestInt(),
                       juce::Justification::centredBottom);
        }
    }

    // Step-number strip along the bottom.
    auto numStrip = getLocalBounds().removeFromBottom(kNumH);
    numStrip.removeFromLeft(kLabelW);
    g.setColour(juce::Colour(0xff808080));
    g.setFont(juce::FontOptions(8.0f));
    const float cw = numStrip.getWidth() / (float)kNumSteps;
    for (int s = 0; s < kNumSteps; ++s)
        g.drawText(juce::String(s + 1),
                   juce::Rectangle<float>(numStrip.getX() + s * cw, (float)numStrip.getY(),
                                          cw, (float)numStrip.getHeight()).toNearestInt(),
                   juce::Justification::centred);
}

// ---- mouse ------------------------------------------------------------------
void ArpPatternGrid::editFromMouse(const juce::MouseEvent& e)
{
    const int step = stepFromX((float)e.x);
    if (step < 0) return;

    // Which lane did the click land in?
    const int y = e.y;
    auto inLane = [&](Lane l) { return laneArea(l).contains(e.x, y); };

    if (inLane(Lane::OnOff))
    {
        // Toggle on click (mouseDown); drag paints the same new state across.
        setOnOff(step, !_on[step], /*notify=*/true);
    }
    else if (inLane(Lane::Accent))
    {
        auto la = laneArea(Lane::Accent);
        const float frac = juce::jlimit(0.0f, 1.0f,
            1.0f - (float)(y - la.getY()) / (float)la.getHeight());
        setAccent(step, (uint8_t)juce::roundToInt(frac * 127.0f), /*notify=*/true);
    }
    else if (inLane(Lane::Ratchet))
    {
        auto la = laneArea(Lane::Ratchet);
        const float frac = juce::jlimit(0.0f, 1.0f,
            1.0f - (float)(y - la.getY()) / (float)la.getHeight());
        const int count = juce::jlimit(1, 4, 1 + (int)(frac * 3.0f + 0.5f));
        setRatchet(step, count, /*notify=*/true);
    }
}

void ArpPatternGrid::mouseDown(const juce::MouseEvent& e) { editFromMouse(e); }
void ArpPatternGrid::mouseDrag(const juce::MouseEvent& e) { editFromMouse(e); }
