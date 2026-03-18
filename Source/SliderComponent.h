/*
 * SliderComponent.h — JT-8000 Custom Vertical Fader
 *
 * Draws a vertical slider with scale markings and a label.
 * Used for ADSR envelope parameters where a fader is more intuitive
 * than a rotary knob.
 *
 * Visual layout (top to bottom):
 *   - Vertical track with scale tick marks on the left
 *   - Filled region from bottom to current value
 *   - Horizontal thumb line at current value
 *   - Label text (grey, below slider)
 *
 * Wraps a hidden juce::Slider for mouse interaction and APVTS binding,
 * identical pattern to KnobComponent.
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class JT8000Slider : public juce::Component,
                      private juce::Slider::Listener
{
public:
    JT8000Slider(const juce::String& label);

    // Bind this slider to an APVTS parameter for automation and state recall
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
    // Repaint when the slider value changes
    void sliderValueChanged(juce::Slider*) override { repaint(); }

    juce::String _labelText;
    juce::Slider _slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> _attachment;

    // Number of visible scale divisions along the track
    static constexpr int kScaleDivisions = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000Slider)
};
