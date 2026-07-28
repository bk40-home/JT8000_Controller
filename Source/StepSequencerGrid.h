/*
 * StepSequencerGrid.h — JT-8000 Step Sequencer Bar Grid
 *
 * Displays 16 vertical bars in a horizontal row.  Click or drag on any
 * bar to set its value (0-127).  Each edit fires sendCCPair() which
 * sends SEQ_STEP_SELECT followed by SEQ_STEP_VALUE to the hardware.
 *
 * The grid maintains its own uint8_t stepValues[16] array.  It does NOT
 * use APVTS attachments because the firmware's step editing protocol
 * is a select-then-value pair, not a single automatable parameter.
 *
 * Visual: orange bars from bottom, step numbers below, matching HTML editor.
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <cstdint>

class StepSequencerGrid : public juce::Component
{
public:
    static constexpr int kNumSteps = 16;

    StepSequencerGrid();

    /* Callback: sends CC pair (SEQ_STEP_SELECT=step, SEQ_STEP_VALUE=value) */
    std::function<void(int step, int value)> onStepChanged;

    /* Send all 16 step values via onStepChanged (used by Send All) */
    void sendAllSteps();

    /* Read/write step values */
    uint8_t getStepValue(int step) const;
    void    setStepValue(int step, uint8_t value, bool notify = false);

    /* Playhead from the engine status feed (Phase J1d).  step -1 or
       running=false clears the marker.  Repaints ONLY the two affected
       bar rects (old + new position) — at 1/16-note rates a full-grid
       repaint would be pure waste. */
    void setPlayhead(int step, bool running);

    /* Bar fill colour — defaults to orange (gate lane); the aux-lane grid
       sets cyan so the two lanes are distinguishable at a glance (D-U1). */
    void setBarColour(juce::Colour c) { _barColour = c; repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    uint8_t _steps[kNumSteps] = {};
    int     _playStep = -1;    // -1 = no marker (stopped / no status yet)
    juce::Colour _barColour { 0xfff08020 };   // gate orange by default

    /* Convert mouse position to step index and value */
    void setFromMouse(const juce::MouseEvent& e);
    int  stepFromX(float x) const;
    int  valueFromY(float y, int step) const;

    /* Bar layout helpers */
    float barWidth() const;
    juce::Rectangle<float> barArea(int step) const;

    static constexpr int kGap      = 3;
    static constexpr int kLabelH   = 14;
    static constexpr int kTopPad   = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerGrid)
};
