/*
 * PluginEditor.h — JT-8000 Controller Main Window
 *
 * Creates the header bar (title, MIDI output picker, channel, buttons)
 * and a scrollable viewport containing all section panels.
 *
 * Changes from previous version:
 *   - layoutSections() now supports multi-column rows using sectionRow[]
 *     from CCDefinitions.h, so ADSR sections sit side-by-side
 *   - buildSections() routes ControlStyle::Slider to addSlider()
 */

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "SectionComponent.h"

class JT8000ControllerEditor : public juce::AudioProcessorEditor
{
public:
    JT8000ControllerEditor(JT8000ControllerProcessor&);
    ~JT8000ControllerEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    JT8000ControllerProcessor& _proc;

    // Header bar widgets
    juce::Label     _titleLabel;
    juce::ComboBox  _midiChSelector;
    juce::ComboBox  _midiOutSelector;
    juce::TextButton _sendAllBtn  { "Send All" };
    juce::TextButton _expandBtn   { "Expand" };
    juce::TextButton _collapseBtn { "Collapse" };

    // Scrollable content area
    juce::Viewport  _viewport;
    juce::Component _contentPanel;

    // One SectionComponent per Section enum value
    juce::OwnedArray<SectionComponent> _sections;

    // Build all sections from kAllParams[] and populate with controls
    void buildSections();

    // Layout sections into the content panel.
    // Supports multi-column rows via sectionRow[] grouping.
    void layoutSections();

    // Refresh the MIDI output device dropdown
    void refreshMidiOutputList();

    static constexpr int kHeaderHeight = 44;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerEditor)
};
