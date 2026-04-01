/*
 * KnobComponent.h — JT-8000 Custom Rotary Knob
 *
 * Draws an arc-style knob with a pointer dot.
 * Wraps a hidden juce::Slider for mouse interaction and APVTS binding.
 *
 * Visual layout (top to bottom):
 *   - Arc track (dark 270° sweep, 7 o'clock to 5 o'clock)
 *   - Arc fill (orange, proportional to value)
 *   - Knob body (dark circle with border)
 *   - Pointer dot (white, follows value angle)
 *   - Label text (grey, below knob)
 *
 * Angle convention:
 *   Screen-space clockwise-from-east — identical to HTML Canvas arc()
 *   and JUCE Path::addCentredArc().
 *     0 rad   = 3 o'clock (east)
 *     +ve     = clockwise on screen
 *     7 o'clk = 135° = kArcStart
 *     5 o'clk = 135° + 270° = kArcStart + kArcSweep
 *
 *   Dot position uses the same convention directly:
 *     dx = cx + r * cos(angle)
 *     dy = cy + r * sin(angle)
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class JT8000Knob : public juce::Component,
                    private juce::Slider::Listener
{
public:
    JT8000Knob(const juce::String& label);

    /* Bind this knob to an APVTS parameter for automation and state recall */
    void setAttachment(juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& paramID);

    void paint(juce::Graphics& g) override;
    void resized() override;

    /* Forward ALL mouse events to the hidden slider for drag interaction.
     * mouseUp() was previously missing — without it the slider can get
     * stuck in a drag state after releasing the mouse button. */
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    /* Repaint when the slider value changes (slider is hidden, we draw custom) */
    void sliderValueChanged(juce::Slider*) override { repaint(); }

    juce::String _labelText;
    juce::Slider _slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> _attachment;

    /* -------------------------------------------------------------------
     * Arc geometry — 270° sweep from 7 o'clock to 5 o'clock
     *
     * Convention: CW from east on screen (matches JUCE addCentredArc and
     *             HTML Canvas arc()).  All values in radians.
     *
     *   7 o'clock = 135° CW from east = 3π/4 ≈ 2.3562 rad
     *   5 o'clock = 135° + 270° = 405° CW ≈ 7.0686 rad
     *   Sweep     = 270° = 3π/2 ≈ 4.7124 rad
     *
     * Matches the firmware TFT (TFTMiniWidgets.h: 225° base + 270° sweep
     * in its north-up convention) and HTML editor (A0=2.36, ASW=4.71).
     * ------------------------------------------------------------------- */
    static constexpr float kArcStart = 2.3562f;   // 7 o'clock (3π/4)
    static constexpr float kArcSweep = 4.7124f;   // 270° total travel (3π/2)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000Knob)
};
