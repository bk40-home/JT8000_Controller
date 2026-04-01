/*
 * JT8000LookAndFeel.cpp — Custom LookAndFeel for JT-8000 Controller
 *
 * drawRotarySlider():
 *   JUCE provides rotaryStartAngle, rotaryEndAngle, and sliderPosProportional.
 *   We interpolate between start and end angles for the arc fill and dot.
 *   Angle convention is JUCE's rotary convention: 0 = 12 o'clock, positive = CW.
 *   The dot position uses the same convention: x = sin(angle), y = -cos(angle).
 *   This is guaranteed correct because JUCE uses these exact angles internally
 *   for mouse tracking — if the dot were wrong, the mouse drag would be wrong too.
 *
 * drawLinearSlider():
 *   JUCE provides sliderPos as the pixel Y position of the thumb.
 *   We draw the track, fill from bottom to thumb, and the thumb bar.
 */

#include "JT8000LookAndFeel.h"

JT8000LookAndFeel::JT8000LookAndFeel()
{
    // Dark theme defaults for combo boxes, labels, scrollbars etc.
    setColour(juce::ComboBox::backgroundColourId,   juce::Colour(0xff0b0e14));
    setColour(juce::ComboBox::textColourId,         juce::Colour(0xffc8d8f4));
    setColour(juce::ComboBox::outlineColourId,      juce::Colour(0xff1e2a3e));
    setColour(juce::PopupMenu::backgroundColourId,  juce::Colour(0xff111620));
    setColour(juce::PopupMenu::textColourId,        juce::Colour(0xffc8d8f4));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff283850));
    setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colour(0xffffffff));
    setColour(juce::Label::textColourId,            juce::Colour(0xff8090b0));
    setColour(juce::ToggleButton::textColourId,     juce::Colour(0xff8090b0));
    setColour(juce::ToggleButton::tickColourId,     juce::Colour(0xfff08020));
}

// -----------------------------------------------------------------------
// Rotary knob — arc track, arc fill, knob body, pointer dot
//
// JUCE's rotary angle convention: 0 = 12 o'clock, positive = clockwise.
// For screen coordinates: x = sin(angle), y = -cos(angle).
// We use exactly the angles JUCE provides — no manual conversion needed.
// -----------------------------------------------------------------------
void JT8000LookAndFeel::drawRotarySlider(juce::Graphics& g,
                                          int x, int y, int width, int height,
                                          float sliderPos,
                                          float rotaryStartAngle,
                                          float rotaryEndAngle,
                                          juce::Slider&)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float cx     = bounds.getCentreX();
    const float cy     = bounds.getCentreY();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.42f;
    const float innerR = radius * 0.72f;
    const float arcR   = radius - 2.0f;

    // Current value angle
    const float valAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // --- Arc track (full sweep, dark) ---
    {
        juce::Path track;
        track.addCentredArc(cx, cy, arcR, arcR, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(_arcTrack);
        g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    // --- Arc fill (orange, proportional to value) ---
    if (sliderPos > 0.005f)
    {
        juce::Path fill;
        fill.addCentredArc(cx, cy, arcR, arcR, 0.0f,
                           rotaryStartAngle, valAngle, true);
        g.setColour(_arcFill);
        g.strokePath(fill, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // --- Knob body (dark circle with border) ---
    g.setColour(_knobBody);
    g.fillEllipse(cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour(_knobBorder);
    g.drawEllipse(cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    // --- Pointer dot ---
    // JUCE rotary convention: x = sin(angle), y = -cos(angle) from centre
    {
        const float dotR  = innerR - 4.0f;
        const float dx    = cx + dotR * std::sin(valAngle);
        const float dy    = cy - dotR * std::cos(valAngle);
        g.setColour(_dot);
        g.fillEllipse(dx - 2.0f, dy - 2.0f, 4.0f, 4.0f);
    }
}

// -----------------------------------------------------------------------
// Vertical fader — track background, orange fill, scale ticks, thumb bar
//
// sliderPos is the pixel Y position of the thumb (provided by JUCE).
// minSliderPos = bottom of track, maxSliderPos = top of track (inverted
// because screen Y increases downward but value increases upward).
// -----------------------------------------------------------------------
void JT8000LookAndFeel::drawLinearSlider(juce::Graphics& g,
                                          int x, int y, int width, int height,
                                          float sliderPos,
                                          float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style,
                                          juce::Slider&)
{
    if (style != juce::Slider::LinearVertical)
    {
        // Fallback for any non-vertical style (shouldn't happen in JT-8000)
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                          sliderPos, minSliderPos, maxSliderPos,
                                          style, *static_cast<juce::Slider*>(nullptr));
        return;
    }

    const float fw     = static_cast<float>(width);
    const float fh     = static_cast<float>(height);
    const float fx     = static_cast<float>(x);
    const float fy     = static_cast<float>(y);

    const float trackW = 6.0f;
    const float trackX = fx + (fw - trackW) * 0.5f;
    const float trackTop    = fy + 4.0f;
    const float trackBottom = fy + fh - 4.0f;
    const float trackH = trackBottom - trackTop;

    if (trackH < 10.0f) return;

    // --- Scale tick marks (left side of track) ---
    {
        g.setColour(_scaleLine);
        const float tickLen = 5.0f;
        const float tickX   = trackX - tickLen - 2.0f;
        for (int i = 0; i <= 4; ++i)
        {
            const float frac = static_cast<float>(i) / 4.0f;
            const float tickY = trackBottom - frac * trackH;
            g.drawHorizontalLine(static_cast<int>(tickY), tickX, trackX - 1.0f);
        }
    }

    // --- Track background ---
    g.setColour(_trackBg);
    g.fillRoundedRectangle(trackX, trackTop, trackW, trackH, 2.0f);
    g.setColour(_trackBorder);
    g.drawRoundedRectangle(trackX, trackTop, trackW, trackH, 2.0f, 0.5f);

    // --- Track fill (orange, from bottom up to thumb position) ---
    // sliderPos is a pixel Y value; trackBottom is the minimum-value position
    if (sliderPos < trackBottom - 1.0f)
    {
        const float fillTop = juce::jmax(trackTop, sliderPos);
        const float fillH   = trackBottom - fillTop;
        g.setColour(_trackFill);
        g.fillRoundedRectangle(trackX, fillTop, trackW, fillH, 2.0f);
    }

    // --- Thumb bar ---
    {
        const float thumbH = 3.0f;
        const float overhang = 6.0f;
        const float thumbX = trackX - overhang;
        const float thumbW = trackW + overhang * 2.0f;
        const float thumbY = juce::jlimit(trackTop, trackBottom, sliderPos);
        g.setColour(_thumb);
        g.fillRoundedRectangle(thumbX, thumbY - thumbH * 0.5f, thumbW, thumbH, 1.5f);
    }
}
