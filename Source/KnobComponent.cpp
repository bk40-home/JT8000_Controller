/*
 * KnobComponent.cpp — JT-8000 Custom Rotary Knob
 *
 * Changes from previous version:
 *   - Removed orange value readout below the label
 *   - Reduced vertical space requirement (height reserved for text: 14px)
 *   - Underscore prefix on all private members
 */

#include "KnobComponent.h"

// -----------------------------------------------------------------------
// Colour palette — matches the JT-8000 dark theme
// -----------------------------------------------------------------------
static const juce::Colour kKnobBody  (0xff141828);   // Knob face fill
static const juce::Colour kKnobBorder(0xff2e3a50);   // Knob outline
static const juce::Colour kArcTrack  (0xff0a1018);   // Full 270° background arc
static const juce::Colour kArcFill   (0xfff08020);   // Orange arc (value indicator)
static const juce::Colour kDot       (0xffc0c8e0);   // Pointer dot on knob face
static const juce::Colour kLabel     (0xff8090b0);   // Label text below knob

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
JT8000Knob::JT8000Knob(const juce::String& label)
    : _labelText(label)
{
    // Hidden slider handles mouse interaction and parameter binding
    _slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    _slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    _slider.setRange(0, 127, 1);
    _slider.setDoubleClickReturnValue(true, 64.0);
    _slider.addListener(this);
    addAndMakeVisible(_slider);
    _slider.setAlpha(0.0f);   // Invisible — we draw custom graphics
}

// -----------------------------------------------------------------------
// APVTS binding
// -----------------------------------------------------------------------
void JT8000Knob::setAttachment(juce::AudioProcessorValueTreeState& apvts,
                                const juce::String& paramID)
{
    _attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramID, _slider);
}

// -----------------------------------------------------------------------
// Layout — hidden slider fills our entire bounds
// -----------------------------------------------------------------------
void JT8000Knob::resized()
{
    _slider.setBounds(getLocalBounds());
}

// -----------------------------------------------------------------------
// Custom paint — arc, knob body, dot, label (no value readout)
// -----------------------------------------------------------------------
void JT8000Knob::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    // Reserve 14px at the bottom for the label text
    const float knobDiam = juce::jmin(w, h - 14.0f);
    const float cx       = w * 0.5f;
    const float cy       = knobDiam * 0.5f + 2.0f;
    const float outerR   = knobDiam * 0.42f;
    const float innerR   = knobDiam * 0.30f;
    const float arcR     = outerR - 2.0f;

    // Normalise slider value to 0..1
    const float norm     = static_cast<float>(_slider.getValue() / 127.0);
    const float valAngle = kStartAngle + norm * (kEndAngle - kStartAngle);
    const float piHalf   = juce::MathConstants<float>::halfPi;

    // --- Arc track (full 270° sweep) ---
    {
        juce::Path track;
        track.addCentredArc(cx, cy, arcR, arcR, 0.0f,
                            kStartAngle + piHalf, kEndAngle + piHalf, true);
        g.setColour(kArcTrack);
        g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    // --- Arc fill (proportional to current value) ---
    if (norm > 0.005f)
    {
        juce::Path fill;
        fill.addCentredArc(cx, cy, arcR, arcR, 0.0f,
                           kStartAngle + piHalf, valAngle + piHalf, true);
        g.setColour(kArcFill);
        g.strokePath(fill, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // --- Knob body (dark circle with border) ---
    g.setColour(kKnobBody);
    g.fillEllipse(cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour(kKnobBorder);
    g.drawEllipse(cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    // --- Pointer dot (positioned on arc at current value angle) ---
    // Clockwise-from-top convention: x = sin(angle), y = -cos(angle)
    {
        const float dotR = innerR - 4.0f;
        const float dx   = cx + dotR * std::sin(valAngle);
        const float dy   = cy - dotR * std::cos(valAngle);
        g.setColour(kDot);
        g.fillEllipse(dx - 2.0f, dy - 2.0f, 4.0f, 4.0f);
    }

    // --- Label (below knob, grey text) ---
    {
        g.setColour(kLabel);
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(_labelText, 0, static_cast<int>(cy + outerR + 2),
                   static_cast<int>(w), 12, juce::Justification::centred);
    }
}

// -----------------------------------------------------------------------
// Mouse event forwarding to the hidden slider
// -----------------------------------------------------------------------
void JT8000Knob::mouseDown(const juce::MouseEvent& e)
{
    _slider.mouseDown(e.getEventRelativeTo(&_slider));
}

void JT8000Knob::mouseDrag(const juce::MouseEvent& e)
{
    _slider.mouseDrag(e.getEventRelativeTo(&_slider));
}

void JT8000Knob::mouseDoubleClick(const juce::MouseEvent& e)
{
    _slider.mouseDoubleClick(e.getEventRelativeTo(&_slider));
}

void JT8000Knob::mouseWheelMove(const juce::MouseEvent& e,
                                 const juce::MouseWheelDetails& w)
{
    _slider.mouseWheelMove(e.getEventRelativeTo(&_slider), w);
}
