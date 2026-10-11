// =============================================================================
// JtEncoderStrip.cpp — implementation
// See JtEncoderStrip.h for the three gestures and the spin animation.
// =============================================================================

#include "JtEncoderStrip.h"

#include <cstdlib>   // std::abs
#include <cmath>     // std::sin, std::cos, std::abs(float)

namespace jt
{

namespace
{
    const juce::Colour kLcdBg  (0xff0a0c10);
    const juce::Colour kBody   (0xff141a28);
    const juce::Colour kOrange (0xfff08020);
    const juce::Colour kBorder (0xff1e2a3e);
}

// =============================================================================
JtEncoderStrip::JtEncoderStrip()
{
    // The dial is painted and hit-tested by this component; no child controls.
    setInterceptsMouseClicks(true, false);
}

void JtEncoderStrip::setKnobImage(juce::Image image)
{
    m_knob = std::move(image);
    repaint();
}

// =============================================================================
void JtEncoderStrip::resized()
{
    // A small inset keeps the face off the edges (the previous dial looked
    // clipped at full bleed).
    m_dial = getLocalBounds().reduced(6);
}

void JtEncoderStrip::paint(juce::Graphics& g)
{
    auto d = m_dial.toFloat();
    const float dia = juce::jmin(d.getWidth(), d.getHeight());
    auto face = juce::Rectangle<float>(dia, dia).withCentre(d.getCentre());

    if (m_knob.isValid())
    {
        // Fit the artwork to the face, then spin it about the centre.
        auto t = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                     .getTransformToFit(m_knob.getBounds().toFloat(), face);
        t = t.rotated(m_angle, face.getCentreX(), face.getCentreY());
        g.drawImageTransformed(m_knob, t);
        return;
    }

    // --- procedural fallback: a clean knob with a spinning pointer -----------
    const auto c = face.getCentre();
    const float r = face.getWidth() * 0.5f;

    g.setColour(kBody);
    g.fillEllipse(face);
    g.setColour(kBorder);
    g.drawEllipse(face, 2.0f);

    // Pointer, rotated by the current angle (0 == 12 o'clock).
    const float px = c.x + std::sin(m_angle) * (r * 0.72f);
    const float py = c.y - std::cos(m_angle) * (r * 0.72f);
    g.setColour(kOrange);
    g.drawLine(c.x, c.y, px, py, 2.5f);

    // Centre cap.
    g.setColour(kLcdBg);
    g.fillEllipse(juce::Rectangle<float>(r * 0.8f, r * 0.8f).withCentre(c));
}

// =============================================================================
// One detent: report it and advance the spin target.
// =============================================================================
void JtEncoderStrip::turn(int dir, bool coarse)
{
    if (onEncoder)
        onEncoder(dir, coarse);
    m_angleTarget += (float) dir * kAnglePerDetent;
    ensureTicking();
}

void JtEncoderStrip::ensureTicking()
{
    if (! isTimerRunning())
        startTimer(kTickMs);
}

// =============================================================================
// Gesture handling
// =============================================================================
void JtEncoderStrip::mouseDown(const juce::MouseEvent& e)
{
    if (! m_dial.contains(e.getPosition()))
        return;

    m_pressing   = true;
    m_dragged    = false;
    m_longFired  = false;
    m_lastDrag   = e.getPosition();
    m_dragAccum  = 0;
    m_pressStart = juce::Time::getMillisecondCounter();
    ensureTicking();                        // timer watches for the hold
}

void JtEncoderStrip::mouseDrag(const juce::MouseEvent& e)
{
    if (! m_pressing)
        return;

    m_dragAccum += (m_lastDrag.y - e.getPosition().y);   // up = increase
    m_lastDrag = e.getPosition();

    while (std::abs(m_dragAccum) >= kPixelsPerDetent)
    {
        const int dir = (m_dragAccum > 0) ? 1 : -1;
        m_dragAccum -= dir * kPixelsPerDetent;
        m_dragged = true;                   // a rotation, so no hold / no click
        turn(dir, e.mods.isShiftDown());
    }
}

void JtEncoderStrip::mouseUp(const juce::MouseEvent& e)
{
    if (m_pressing && ! m_dragged && ! m_longFired
        && m_dial.contains(e.getPosition()) && onEnter)
        onEnter();

    m_pressing  = false;
    m_dragged   = false;
    m_longFired = false;
    // The timer keeps running until the spin settles, then stops itself.
}

void JtEncoderStrip::mouseWheelMove(const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& w)
{
    if (w.deltaY == 0.0f)
        return;
    turn((w.deltaY > 0.0f) ? 1 : -1, e.mods.isShiftDown());
}

// =============================================================================
// One timer serves both jobs: fire the hold at its threshold, and ease the
// spin toward its target. It stops itself once neither has work.
// =============================================================================
void JtEncoderStrip::timerCallback()
{
    // Hold -> BACK, fired once, while still pressed and not turned.
    if (m_pressing && ! m_dragged && ! m_longFired
        && (juce::Time::getMillisecondCounter() - m_pressStart) >= (uint32_t) kHoldMs)
    {
        m_longFired = true;
        if (onBack)
            onBack();
    }

    // Ease the rendered angle toward its target.
    const float diff = m_angleTarget - m_angle;
    if (std::abs(diff) > 0.001f)
    {
        m_angle += diff * kEase;
        repaint();
    }
    else
    {
        m_angle = m_angleTarget;
    }

    // Nothing left to do? Stop until the next gesture.
    if (! m_pressing && std::abs(m_angleTarget - m_angle) <= 0.001f)
        stopTimer();
}

} // namespace jt
