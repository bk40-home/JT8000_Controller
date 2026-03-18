/*
 * KnobComponent.h — JT-8000 Custom Rotary Knob
 *
 * Draws an arc-style knob with a pointer dot.
 * Wraps a hidden juce::Slider for mouse interaction and APVTS binding.
 *
 * Visual layout (top to bottom):
 *   - Arc track (dark 270° sweep)
 *   - Arc fill (orange, proportional to value)
 *   - Knob body (dark circle with border)
 *   - Pointer dot (white, follows value angle)
 *   - Label text (grey, below knob)
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class JT8000Knob : public juce::Component,
                    private juce::Slider::Listener
{
public:
    JT8000Knob(const juce::String& label);

    // Bind this knob to an APVTS parameter for automation and state recall
    void setAttachment(juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& paramID);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Forward mouse events to the hidden slider for drag interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    // Repaint when the slider value changes (slider is hidden, we draw custom)
    void sliderValueChanged(juce::Slider*) override { repaint(); }

    juce::String _labelText;
    juce::Slider _slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> _attachment;

    // Arc sweep angles in radians (clockwise from 12 o'clock)
    // 7 o'clock = -3.0 rad, 5 o'clock = +3.0 rad (~270° sweep)
    static constexpr float kStartAngle = -3.0f;
    static constexpr float kEndAngle   =  3.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000Knob)
};
