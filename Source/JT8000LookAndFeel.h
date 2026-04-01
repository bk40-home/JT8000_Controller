/*
 * JT8000LookAndFeel.h — Custom LookAndFeel for JT-8000 Controller
 *
 * Overrides:
 *   drawRotarySlider()  — dark arc knob with orange fill and pointer dot
 *   drawLinearSlider()  — dark vertical fader with orange fill and thumb bar
 *
 * All angle maths for the rotary knob are handled by JUCE internally.
 * The drawRotarySlider() callback receives rotaryStartAngle and rotaryEndAngle
 * which match the values passed to Slider::setRotaryParameters(). The
 * sliderPosProportional parameter is a normalised 0-1 value.
 *
 * This eliminates all hand-rolled sin/cos, angle conventions, and mouse
 * event forwarding that caused bugs in the previous KnobComponent.
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class JT8000LookAndFeel : public juce::LookAndFeel_V4
{
public:
    JT8000LookAndFeel();

    // --- Rotary knob: dark circle + orange arc + pointer dot ---
    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    // --- Vertical fader: dark track + orange fill + thumb bar ---
    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

    // --- Rotary parameters (standard 270° sweep, 7 o'clock to 5 o'clock) ---
    // These are the values passed to Slider::setRotaryParameters().
    // JUCE convention: 0 = 12 o'clock, positive = clockwise on screen.
    // 7 o'clock = 5π/4 rad,  5 o'clock = -π/4 + 2π = 7π/4 rad.
    // (JUCE's rotary uses a 12-o'clock-based CW convention internally.)
    static constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.25f;   // 225° = 7 o'clock
    static constexpr float kRotaryEnd   = juce::MathConstants<float>::pi * 2.75f;   // 495° = 5 o'clock (wrap)

private:
    // Colour palette — matches the JT-8000 dark theme
    juce::Colour _knobBody   { 0xff141828 };
    juce::Colour _knobBorder { 0xff2e3a50 };
    juce::Colour _arcTrack   { 0xff0a1018 };
    juce::Colour _arcFill    { 0xfff08020 };
    juce::Colour _dot        { 0xffc0c8e0 };

    juce::Colour _trackBg     { 0xff0a1018 };
    juce::Colour _trackFill   { 0xfff08020 };
    juce::Colour _trackBorder { 0xff2e3a50 };
    juce::Colour _thumb       { 0xffc0c8e0 };
    juce::Colour _scaleLine   { 0xff1e2a3e };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000LookAndFeel)
};
