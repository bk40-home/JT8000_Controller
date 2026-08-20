/*
 * PluginEditor.h — JT-8000 Controller Main Window
 *
 * Owns the JT8000LookAndFeel instance and hosts the RmlUi panel.
 * Header bar with MIDI routing, SYX import, patch save/load, expand/collapse.
 *
 * VST3 NOTES:
 *   - MIDI Out selector is shown in BOTH standalone and VST3 modes.
 *     Most VST3 hosts (including Ableton) silently drop SysEx messages
 *     from plugin MIDI output. The direct MIDI output device bypasses
 *     the host's MIDI routing so SysEx reaches the Teensy reliably.
 *   - MIDI In selector is standalone-only. Incoming channel messages
 *     (notes, CCs, pitch bend) are routed by the host in VST3 mode.
 *   - SYX patch picker uses a non-modal overlay (SyxPickerOverlay) instead
 *     of enterModalState(), which is illegal in VST3 hosts.
 *   - Incoming CCs from hardware are reflected into the APVTS by the
 *     processor, so the UI automatically tracks external changes.
 */

#pragma once

#include "JtRmlPanel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
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

    juce::Label     _patchNameLabel;
    juce::ComboBox  _midiOutSelector;
    juce::TextButton _midiInButton { "MIDI In" };   // opens a tickable device
                                                    // menu (multi-select). Was a
                                                    // ComboBox; multi-input needs
                                                    // checkable items.

    /* Points the RmlUi skin loader at a folder on disk (containing an exported
     * jt8000.rml). Files there win over the embedded skin and the document is
     * reloaded, so a new layout appears with no rebuild. Lives in the JUCE
     * header strip so it is always present, independent of whatever RML is
     * currently loaded. */
    juce::TextButton _loadSkinButton { "Skin\u2026" };

    /* Clears the skin disk-root and reloads, returning to the embedded factory
     * jt8000.rml (all controls, the pristine layout). Lets you get back to the
     * original after trying a custom skin, without relaunching. */
    juce::TextButton _factorySkinButton { "Factory" };

    // ---- Perf-Global header controls (Phase 2) ----------------------------
    // Rendered on the third header row. Bound to APVTS params (CCs 140..146)
    // via attachments so state save/restore Just Works.

    juce::ComboBox  _engineModeCombo;      // Controller / Sound / Both (J2 —
                                           // hidden when built without engine)

    // APVTS attachments for the perf-global header controls. OwnedArray so
    // we can construct them all at once after createEditor() / APVTS exists.


    /* The RmlUi panel replaces the accordion. The 34px strip above it keeps
     * only what cannot come from the document: MIDI device lists are discovered
     * at runtime. */
    std::unique_ptr<jt::JtRmlPanel> _panel;

    void refreshMidiOutputList();
    void showMidiInputMenu();      // pops the tickable multi-select device menu

    void loadSyxFile();
    void showBankPicker(const juce::String& fileName);
    std::unique_ptr<juce::FileChooser> _fileChooser;
    juce::MemoryBlock _syxBuffer;
    JP8000SyxImporter::ParsedBank _syxBank;
    std::unique_ptr<SyxPickerOverlay> _syxOverlay;

    void savePatchFile();
    void loadPatchFile();
    void initPatch();

    /* Opens a folder chooser and, on selection, sets that folder as the skin
     * disk-root on the panel (which reloads the document). See _loadSkinButton. */
    void loadSkinFolder();

    void valueTreePropertyChanged(juce::ValueTree& tree,
                                  const juce::Identifier& property) override;
    void timerCallback() override;
    void applyEngineSampleRatePolicy();   // J2: standalone 44.1 k request

    // ---- Status poll bookkeeping (J1d) -------------------------------------
    // One timer, two cadences: the tick runs at kStatusPollMs for the status
    // feed; the MIDI-device hot-plug scan (the old 2 s job) runs every
    // kDevicePollTicks ticks so its cost is unchanged.  Liveness = the
    // processor's status counter moved within kLinkStaleTicks ticks (the
    // firmware heartbeats at >= 1 Hz, so ~3 s of silence means wire dead).
    static constexpr int kStatusPollMs    = 50;
    static constexpr int kDevicePollTicks = 40;   // 40 * 50 ms = 2 s, as before
    static constexpr int kLinkStaleTicks  = 60;   // 3 s without a heartbeat
    int      _pollTick        = 0;
    uint32_t _lastStatusCount = 0;
    int      _ticksSinceBeat  = kLinkStaleTicks;  // start "stale" until proven alive

    // Header layout constants
    //   Row 1: Title, patch name, MIDI IO selectors        — 36px
    //   Row 2: Send All / Load SYX / Save / Load / Init
    //          / Expand / Collapse                         — 36px
    //   Row 3: Layers + Perf Global controls               — 36px
    // Total = 108px. Was 72 before Phase 2 (two rows).
    /* 108 -> 34. Row 3 was entirely parameters and now lives in the panel's
     * Setup layer; row 2's buttons are painted into the skin's header. */
    static constexpr int kHeaderHeight = 34;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerEditor)
};
