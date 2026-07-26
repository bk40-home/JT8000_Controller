/*
 * StepSequencerGrid.cpp — JT-8000 Step Sequencer Bar Grid
 *
 * 16 vertical bars in a horizontal row.  Click/drag sets bar height
 * (0-127 unipolar: 0 = empty, 127 = full).  Each change fires the
 * onStepChanged callback so the editor can send the CC pair.
 */

#include "StepSequencerGrid.h"

// Colours matching the JT-8000 dark theme
static const juce::Colour kBarBg     (0xff0a1018);   // Bar well background
static const juce::Colour kBarBorder (0xff1a2030);   // Bar well border
static const juce::Colour kBarFill   (0xfff08020);   // Orange fill
static const juce::Colour kBarText   (0xff8090b0);   // Step number text
static const juce::Colour kBarVal    (0xffc8d8f4);   // Value label text
static const juce::Colour kPlayhead  (0xff28ff58);   // Engine playhead (theme green)

StepSequencerGrid::StepSequencerGrid()
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

uint8_t StepSequencerGrid::getStepValue(int step) const
{
    if (step < 0 || step >= kNumSteps) return 0;
    return _steps[step];
}

void StepSequencerGrid::setStepValue(int step, uint8_t value, bool notify)
{
    if (step < 0 || step >= kNumSteps) return;
    _steps[step] = value;
    repaint();
    if (notify && onStepChanged)
        onStepChanged(step, value);
}

void StepSequencerGrid::sendAllSteps()
{
    if (!onStepChanged) return;
    for (int i = 0; i < kNumSteps; ++i)
        onStepChanged(i, _steps[i]);
}

// -----------------------------------------------------------------------
// Layout helpers
// -----------------------------------------------------------------------
float StepSequencerGrid::barWidth() const
{
    const float totalGaps = static_cast<float>((kNumSteps - 1) * kGap);
    return (static_cast<float>(getWidth()) - totalGaps) / static_cast<float>(kNumSteps);
}

juce::Rectangle<float> StepSequencerGrid::barArea(int step) const
{
    const float bw   = barWidth();
    const float xPos = static_cast<float>(step) * (bw + static_cast<float>(kGap));
    const float top  = static_cast<float>(kTopPad);
    const float bot  = static_cast<float>(getHeight() - kLabelH);
    return { xPos, top, bw, bot - top };
}

int StepSequencerGrid::stepFromX(float x) const
{
    const float bw = barWidth();
    const float stride = bw + static_cast<float>(kGap);
    int step = static_cast<int>(x / stride);
    return juce::jlimit(0, kNumSteps - 1, step);
}

int StepSequencerGrid::valueFromY(float y, int step) const
{
    auto area = barArea(step);
    // y at top of bar = 127, y at bottom = 0
    float norm = 1.0f - (y - area.getY()) / area.getHeight();
    return juce::jlimit(0, 127, juce::roundToInt(norm * 127.0f));
}

// -----------------------------------------------------------------------
// Mouse interaction
// -----------------------------------------------------------------------
void StepSequencerGrid::setFromMouse(const juce::MouseEvent& e)
{
    int step = stepFromX(static_cast<float>(e.x));
    int val  = valueFromY(static_cast<float>(e.y), step);
    auto prev = _steps[step];

    _steps[step] = static_cast<uint8_t>(val);
    repaint();

    if (onStepChanged && static_cast<int>(prev) != val)
        onStepChanged(step, val);
}

// -----------------------------------------------------------------------
// setPlayhead — engine playhead marker (Phase J1d)
//
// Called from the editor's status poll.  Change-driven: identical state
// returns without painting, and a change repaints only the two bar rects
// involved (the ESP32's dirty-rect discipline, applied here too).
// -----------------------------------------------------------------------
void StepSequencerGrid::setPlayhead(int step, bool running)
{
    const int next = (running && step >= 0 && step < kNumSteps) ? step : -1;
    if (next == _playStep)
        return;

    const int prev = _playStep;
    _playStep = next;

    // Bar rects exclude the number strip; pad 2px for the marker stroke.
    if (prev >= 0) repaint(barArea(prev).expanded(2.0f).getSmallestIntegerContainer());
    if (next >= 0) repaint(barArea(next).expanded(2.0f).getSmallestIntegerContainer());
}

void StepSequencerGrid::mouseDown(const juce::MouseEvent& e)
{
    setFromMouse(e);
}

void StepSequencerGrid::mouseDrag(const juce::MouseEvent& e)
{
    setFromMouse(e);
}

// -----------------------------------------------------------------------
// Paint — 16 vertical bars with orange fill and step numbers
// -----------------------------------------------------------------------
void StepSequencerGrid::paint(juce::Graphics& g)
{
    const float bw = barWidth();

    for (int i = 0; i < kNumSteps; ++i)
    {
        auto area = barArea(i);

        // Bar well (dark background with border); the engine's current step
        // gets a green ring instead of the plain border (Phase J1d playhead).
        g.setColour(kBarBg);
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(i == _playStep ? kPlayhead : kBarBorder);
        g.drawRoundedRectangle(area, 3.0f, i == _playStep ? 1.5f : 1.0f);

        // Orange fill from bottom up
        const float norm = static_cast<float>(_steps[i]) / 127.0f;
        if (norm > 0.005f)
        {
            const float fillH = norm * area.getHeight();
            g.setColour(kBarFill);
            g.fillRoundedRectangle(area.getX(), area.getBottom() - fillH,
                                   area.getWidth(), fillH, 2.0f);
        }

        // Value label at top of bar
        {
            int pct = juce::roundToInt(norm * 100.0f);
            g.setColour(kBarVal);
            g.setFont(juce::FontOptions(7.0f));
            g.drawText(juce::String(pct) + "%",
                       static_cast<int>(area.getX()), static_cast<int>(area.getY()) + 1,
                       static_cast<int>(area.getWidth()), 10,
                       juce::Justification::centred);
        }

        // Step number below bar
        {
            g.setColour(kBarText);
            g.setFont(juce::FontOptions(8.0f));
            g.drawText(juce::String(i + 1),
                       static_cast<int>(area.getX()),
                       getHeight() - kLabelH,
                       static_cast<int>(bw), kLabelH,
                       juce::Justification::centred);
        }
    }
}
