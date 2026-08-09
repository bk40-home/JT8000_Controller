/*
 * ArpPatternGrid.h — JT-8000 Arpeggiator tri-lane pattern editor (Phase 9)
 *
 * 16 columns × 3 lanes, stacked vertically:
 *
 *   [ On/Off ]  16 toggle cells — lit orange = step plays, dark = rest.
 *   [ Accent  ] 16 vertical bars 0..127 — per-step velocity scale.
 *   [ Ratchet ] 16 mini-bars quantised to 1..4 — per-step re-triggers.
 *
 * Like StepSequencerGrid, this does NOT use APVTS: the firmware's per-step
 * protocol is select-then-value (arp.step_select then the lane param), which
 * doesn't map to a single automatable parameter.  Each edit fires
 * onLaneEdit(step, lane, value); the processor turns that into the
 * ArpParamId NRPN pair (see PluginProcessor::sendArpStepEdit).
 *
 * value semantics per lane:
 *   OnOff   : 0 or 127   (firmware norm>=0.5 threshold)
 *   Accent  : 0..127     (norm velocity scale)
 *   Ratchet : 0..127 encoding 1..4 via the firmware's lroundf(norm*3)+1 lattice
 *             — the grid sends the lattice byte, not the raw 1..4 count.
 *
 * Visual language matches the sequencer grid: orange fills, step numbers,
 * playhead marker fed from the engine status word.
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <cstdint>

class ArpPatternGrid : public juce::Component
{
public:
    static constexpr int kNumSteps = 16;

    // Which lane an edit targets.  Order is internal to the widget only.
    enum class Lane : int { OnOff = 0, Accent = 1, Ratchet = 2 };

    ArpPatternGrid();

    // Callback: (step 0..15, lane, value 0..127 already in wire form).
    std::function<void(int step, Lane lane, int value)> onLaneEdit;

    // Push every step of every lane out via onLaneEdit (used by "Send All").
    void sendAll();

    // Read/write model (no notify by default — matches StepSequencerGrid).
    void setOnOff  (int step, bool on,   bool notify = false);
    void setAccent (int step, uint8_t v, bool notify = false);
    void setRatchet(int step, int count, bool notify = false);   // count 1..4

    bool    getOnOff  (int step) const;
    uint8_t getAccent (int step) const;
    int     getRatchet(int step) const;                          // 1..4

    // Playhead from the engine status feed — repaints only affected columns.
    void setPlayhead(int step, bool running);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    // --- model ---
    bool     _on     [kNumSteps];
    uint8_t  _accent [kNumSteps];
    uint8_t  _ratchet[kNumSteps];   // stored 1..4
    int      _playStep = -1;

    // --- colours (match the sequencer/orange theme) ---
    juce::Colour _fill   { 0xfff08020 };   // gate orange
    juce::Colour _dim    { 0x40f08020 };   // dark orange (rest / empty)
    juce::Colour _accCol { 0xfff0a040 };   // accent bars (lighter orange)
    juce::Colour _ratCol { 0xff20d0f0 };   // ratchet (cyan, echoes aux lane)

    // --- lane geometry ---
    // The three lanes split the height; labels sit on the left gutter.
    static constexpr int kGap     = 3;
    static constexpr int kLabelW  = 46;   // left gutter for lane names
    static constexpr int kNumH    = 14;   // step-number strip at the bottom
    static constexpr int kTopPad  = 2;

    juce::Rectangle<int> laneArea(Lane lane) const;   // full lane strip
    juce::Rectangle<float> cellArea(Lane lane, int step) const;
    int  stepFromX(float x) const;
    void editFromMouse(const juce::MouseEvent& e);

    // The ratchet byte the firmware decodes back to 1..4 (lroundf(norm*3)+1).
    static int ratchetToByte(int count);   // 1..4 -> {0,42,85,127}-ish lattice
    static int byteToRatchet(int byte);    // inverse for display

    // Accent/onoff wire bytes.
    static int onOffToByte(bool on) { return on ? 127 : 0; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpPatternGrid)
};
