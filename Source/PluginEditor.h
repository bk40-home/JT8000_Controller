/*
 * PluginEditor.h — JT-8000 Controller Main Window
 *
 * Owns the JT8000LookAndFeel instance and passes it to all SectionComponents.
 * Header bar with MIDI routing, SYX import, patch save/load, expand/collapse.
 *
 * VST3 NOTES:
 *   - MIDI In/Out selectors are only shown in standalone mode.
 *     In VST3, the host routes MIDI — manual device selection is unnecessary.
 *   - SYX patch picker uses a non-modal overlay (SyxPickerOverlay) instead
 *     of enterModalState(), which is illegal in VST3 hosts.
 *   - Incoming CCs from hardware are reflected into the APVTS by the
 *     processor, so the UI automatically tracks external changes.
 */

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "SectionComponent.h"
#include "JT8000LookAndFeel.h"
#include "JP8000SyxImporter.h"
#include "SyxPickerOverlay.h"
#include "TouchSafeViewport.h"

// =============================================================================
// Main Editor
// =============================================================================
class JT8000ControllerEditor : public juce::AudioProcessorEditor,
                                private juce::ValueTree::Listener,
                                private juce::Timer
{
public:
    JT8000ControllerEditor(JT8000ControllerProcessor&);
    ~JT8000ControllerEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    JT8000ControllerProcessor& _proc;
    JT8000LookAndFeel _lnf;
    bool _isStandalone = false;

    juce::Label     _titleLabel;
    juce::Label     _patchNameLabel;
    juce::ComboBox  _midiOutSelector;
    juce::ComboBox  _midiInSelector;

    // ---- Perf-Global header controls (Phase 2) ----------------------------
    // Rendered on the third header row. Bound to APVTS params (CCs 140..146)
    // via attachments so state save/restore Just Works.
    juce::Label     _perfLabel;            // "Layers:" static label
    juce::ComboBox  _editTargetCombo;      // perf_edit_target (A/B/Both)
    juce::ComboBox  _chACombo;             // perf_midi_ch_a (Ch 1..16)
    juce::ComboBox  _chBCombo;             // perf_midi_ch_b (Ch 1..16)

    juce::Label     _perfSetLabel;         // "Perf:" static label
    juce::ComboBox  _perfModeCombo;        // perf_mode (Single/Layer/Split)
    juce::Slider    _voiceSplitKnob;       // perf_voice_split
    juce::Slider    _splitNoteKnob;        // perf_split_note
    juce::Slider    _balanceKnob;          // perf_balance

    // APVTS attachments for the perf-global header controls. OwnedArray so
    // we can construct them all at once after createEditor() / APVTS exists.
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> _headerComboAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment>   _headerSliderAtts;

    juce::TextButton _sendAllBtn   { "Send All" };
    juce::TextButton _loadSyxBtn   { "Load SYX" };
    juce::TextButton _savePatchBtn { "Save" };
    juce::TextButton _loadPatchBtn { "Load" };
    juce::TextButton _initPatchBtn { "Init" };
    juce::TextButton _expandBtn    { "Expand" };
    juce::TextButton _collapseBtn  { "Collapse" };

    TouchSafeViewport _viewport;
    juce::Component _contentPanel;
    juce::OwnedArray<SectionComponent> _sections;

    void buildSections();
    void layoutSections();
    void refreshMidiOutputList();
    void refreshMidiInputList();

    void loadSyxFile();
    void showBankPicker(const juce::String& fileName);
    std::unique_ptr<juce::FileChooser> _fileChooser;
    juce::MemoryBlock _syxBuffer;
    JP8000SyxImporter::ParsedBank _syxBank;
    std::unique_ptr<SyxPickerOverlay> _syxOverlay;

    void savePatchFile();
    void loadPatchFile();
    void initPatch();

    void valueTreePropertyChanged(juce::ValueTree& tree,
                                  const juce::Identifier& property) override;
    void timerCallback() override;

    // Header layout constants
    //   Row 1: Title, patch name, MIDI IO selectors        — 36px
    //   Row 2: Send All / Load SYX / Save / Load / Init
    //          / Expand / Collapse                         — 36px
    //   Row 3: Layers + Perf Global controls               — 36px
    // Total = 108px. Was 72 before Phase 2 (two rows).
    static constexpr int kHeaderHeight = 108;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerEditor)
};
