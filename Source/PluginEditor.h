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
#include "TouchSafeViewport.h"

// =============================================================================
// SyxPickerOverlay — non-modal SYX patch picker
//
// Replaces enterModalState() AlertWindow which is illegal in VST3 hosts.
// Draws a dimmed overlay covering the editor with a patch list and buttons.
// =============================================================================
class SyxPickerOverlay : public juce::Component
{
public:
    std::function<void(int result, int patchIndex)> onResult;

    SyxPickerOverlay(const juce::String& title,
                     const juce::StringArray& patchNames)
    {
        _titleLabel.setText(title, juce::dontSendNotification);
        _titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc8d8f4));
        _titleLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        _titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(_titleLabel);

        for (int i = 0; i < patchNames.size(); ++i)
            _combo.addItem(patchNames[i], i + 1);
        _combo.setSelectedItemIndex(0, juce::dontSendNotification);
        _combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b0e14));
        _combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffc8d8f4));
        _combo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff1e2a3e));
        addAndMakeVisible(_combo);

        auto styleBtn = [](juce::TextButton& b, juce::Colour c)
        {
            b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff161c28));
            b.setColour(juce::TextButton::textColourOffId, c);
        };

        styleBtn(_loadSendBtn, juce::Colour(0xfff08020));
        styleBtn(_loadOnlyBtn, juce::Colour(0xff28ff58));
        styleBtn(_cancelBtn,   juce::Colour(0xff8090b0));

        _loadSendBtn.onClick = [this] { finish(1); };
        _loadOnlyBtn.onClick = [this] { finish(2); };
        _cancelBtn.onClick   = [this] { finish(0); };

        addAndMakeVisible(_loadSendBtn);
        addAndMakeVisible(_loadOnlyBtn);
        addAndMakeVisible(_cancelBtn);

        setAlwaysOnTop(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xcc000000));
        g.setColour(juce::Colour(0xff111620));
        g.fillRoundedRectangle(_dialogBounds.toFloat(), 8.0f);
        g.setColour(juce::Colour(0xff1e2a3e));
        g.drawRoundedRectangle(_dialogBounds.toFloat(), 8.0f, 1.5f);
    }

    void resized() override
    {
        const int dlgW = juce::jmin(420, getWidth() - 40);
        const int dlgH = 180;
        _dialogBounds = getLocalBounds().withSizeKeepingCentre(dlgW, dlgH);

        auto area = _dialogBounds.reduced(16);
        _titleLabel.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
        _combo.setBounds(area.removeFromTop(28));
        area.removeFromTop(12);

        auto btnRow = area.removeFromTop(30);
        const int btnW = (btnRow.getWidth() - 16) / 3;
        _loadSendBtn.setBounds(btnRow.removeFromLeft(btnW));
        btnRow.removeFromLeft(8);
        _loadOnlyBtn.setBounds(btnRow.removeFromLeft(btnW));
        btnRow.removeFromLeft(8);
        _cancelBtn.setBounds(btnRow);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!_dialogBounds.contains(e.getPosition()))
            finish(0);
    }

    int getSelectedIndex() const { return _combo.getSelectedItemIndex(); }

private:
    void finish(int result)
    {
        if (onResult)
            onResult(result, _combo.getSelectedItemIndex());
        if (auto* parent = getParentComponent())
            parent->removeChildComponent(this);
    }

    juce::Label    _titleLabel;
    juce::ComboBox _combo;
    juce::TextButton _loadSendBtn { "Load + Send" };
    juce::TextButton _loadOnlyBtn { "Load Only" };
    juce::TextButton _cancelBtn   { "Cancel" };
    juce::Rectangle<int> _dialogBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyxPickerOverlay)
};

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
    juce::ComboBox  _midiChSelector;

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
    void showPatchPicker(const juce::String& fileName);
    std::unique_ptr<juce::FileChooser> _fileChooser;
    juce::MemoryBlock _syxBuffer;
    std::vector<JP8000SyxImporter::Patch> _syxPatches;
    std::unique_ptr<SyxPickerOverlay> _syxOverlay;

    void savePatchFile();
    void loadPatchFile();
    void initPatch();

    void valueTreePropertyChanged(juce::ValueTree& tree,
                                  const juce::Identifier& property) override;
    void timerCallback() override;

    static constexpr int kHeaderHeight = 72;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerEditor)
};
