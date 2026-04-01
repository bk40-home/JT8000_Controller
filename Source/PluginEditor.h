/*
 * PluginEditor.h — JT-8000 Controller Main Window
 *
 * Owns the JT8000LookAndFeel instance and passes it to all SectionComponents.
 * Header bar with MIDI routing, SYX import, patch save/load, expand/collapse.
 *
 * ADDITIONS (v2):
 *   - LCD-style patch name label (matches HTML editor's LCD display).
 *   - MIDI input device selector (standalone mode — forwards to output).
 *   - Init Patch button (resets all parameters to defaults).
 *   - ValueTree listener on patch name property for automatic LCD updates.
 *   - Timer for periodic MIDI device list refresh (hot-plug support).
 */

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "SectionComponent.h"
#include "JT8000LookAndFeel.h"
#include "JP8000SyxImporter.h"
#include "TouchSafeViewport.h"

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

    // --- Header controls (row 1: branding + MIDI routing) -------------------
    juce::Label     _titleLabel;
    juce::Label     _patchNameLabel;       // LCD-style patch name display
    juce::ComboBox  _midiOutSelector;
    juce::ComboBox  _midiInSelector;       // MIDI input (standalone only)
    juce::ComboBox  _midiChSelector;

    // --- Header controls (row 2: actions) -----------------------------------
    juce::TextButton _sendAllBtn   { "Send All" };
    juce::TextButton _loadSyxBtn   { "Load SYX" };
    juce::TextButton _savePatchBtn { "Save" };
    juce::TextButton _loadPatchBtn { "Load" };
    juce::TextButton _initPatchBtn { "Init" };
    juce::TextButton _expandBtn    { "Expand" };
    juce::TextButton _collapseBtn  { "Collapse" };

    // --- Scrollable content -------------------------------------------------
    TouchSafeViewport _viewport;
    juce::Component _contentPanel;
    juce::OwnedArray<SectionComponent> _sections;

    void buildSections();
    void layoutSections();

    // --- MIDI device management ---------------------------------------------
    void refreshMidiOutputList();
    void refreshMidiInputList();

    // --- SYX import ---------------------------------------------------------
    void loadSyxFile();
    void showPatchPicker(const juce::String& fileName);
    std::unique_ptr<juce::FileChooser> _fileChooser;
    juce::MemoryBlock _syxBuffer;
    std::vector<JP8000SyxImporter::Patch> _syxPatches;

    // --- Patch file save / load ---------------------------------------------
    void savePatchFile();
    void loadPatchFile();

    // --- Init patch ---------------------------------------------------------
    void initPatch();

    // --- ValueTree::Listener (patch name changes) ---------------------------
    void valueTreePropertyChanged(juce::ValueTree& tree,
                                  const juce::Identifier& property) override;

    // --- Timer (periodic MIDI device refresh) --------------------------------
    void timerCallback() override;

    // --- Layout constants ---------------------------------------------------
    static constexpr int kHeaderHeight = 72;   // Two-row header (was 44)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerEditor)
};
