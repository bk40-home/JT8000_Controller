/*
 * PluginEditor.cpp — JT-8000 Controller Main Window
 *
 * Two-row header layout:
 *   Row 1: Title | LCD Patch Name | [MIDI Out | MIDI In]* | Channel
 *   Row 2: Send All | Load SYX | Save | Load | Init | Expand | Collapse
 *
 * * MIDI Out and MIDI In selectors only visible in standalone mode.
 */

#include "PluginEditor.h"

static const juce::Colour kBg       (0xff0b0e14);
static const juce::Colour kHeaderBg (0xff141a28);
static const juce::Colour kBorder   (0xff1e2a3e);
static const juce::Colour kOrange   (0xfff08020);
static const juce::Colour kGreen    (0xff28ff58);
static const juce::Colour kPurple   (0xff9040ff);
static const juce::Colour kTextHi   (0xffc8d8f4);
static const juce::Colour kText     (0xff8090b0);
static const juce::Colour kLcdBg    (0xff0a0c10);
static const juce::Colour kLcdText  (0xfff08020);

static const juce::Identifier kPatchNameProp("patchName");

// =============================================================================
// Constructor
// =============================================================================
JT8000ControllerEditor::JT8000ControllerEditor(JT8000ControllerProcessor& p)
    : AudioProcessorEditor(p), _proc(p)
{
    setLookAndFeel(&_lnf);
    _isStandalone = _proc.isStandaloneMode();

    // --- Title label --------------------------------------------------------
    _titleLabel.setText("JT-8000", juce::dontSendNotification);
    _titleLabel.setColour(juce::Label::textColourId, kOrange);
    _titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(_titleLabel);

    // --- LCD patch name display ---------------------------------------------
    _patchNameLabel.setText(_proc.getPatchName(), juce::dontSendNotification);
    _patchNameLabel.setColour(juce::Label::textColourId, kLcdText);
    _patchNameLabel.setColour(juce::Label::backgroundColourId, kLcdBg);
    _patchNameLabel.setColour(juce::Label::outlineColourId, kBorder);
    _patchNameLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    _patchNameLabel.setJustificationType(juce::Justification::centred);
    _patchNameLabel.setEditable(true, true, false);
    _patchNameLabel.onTextChange = [this]
    {
        _proc.setPatchName(_patchNameLabel.getText());
    };
    addAndMakeVisible(_patchNameLabel);

    _proc.apvts.state.addListener(this);

    // =========================================================================
    // Perf-Global header controls (Phase 2 — see PluginProcessor.h commentary)
    //
    // These replace the single MIDI-channel selector that existed pre-Phase-2.
    // All seven perf CCs (140..146) now have visible header controls.
    // Bound to APVTS via attachments — state save/restore works automatically.
    //
    // Visual layout (bottom header row, left to right):
    //   [Layers:]  [Edit Target A/B/Both]  [Ch A 1..16]  [Ch B 1..16]
    //   [Perf:]    [Mode Single/Layer/Split]  [Voice]  [Split]  [Balance]
    // =========================================================================

    auto styleLabel = [](juce::Label& lbl, const juce::String& text)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setColour(juce::Label::textColourId, kText);
        lbl.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        lbl.setJustificationType(juce::Justification::centredRight);
    };
    styleLabel(_perfLabel,    "Layers:");
    styleLabel(_perfSetLabel, "Perf:");
    addAndMakeVisible(_perfLabel);
    addAndMakeVisible(_perfSetLabel);

    auto styleCombo = [](juce::ComboBox& cb)
    {
        cb.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b0e14));
        cb.setColour(juce::ComboBox::textColourId,       kTextHi);
        cb.setColour(juce::ComboBox::outlineColourId,    kBorder);
    };
    auto styleKnob = [](juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    };

    // Populate combo items — must match the ccLUT-based choice ordering
    // in CCDefinitions.h so the ComboBoxAttachment below lines up 1:1.
    // (JUCE ComboBox::addItem IDs are 1-based; the attachment maps
    //  index 0..N-1 to combo IDs 1..N.)
    _editTargetCombo.addItem("A",    1);
    _editTargetCombo.addItem("B",    2);
    _editTargetCombo.addItem("Both", 3);

    _perfModeCombo.addItem("Single", 1);
    _perfModeCombo.addItem("Layer",  2);
    _perfModeCombo.addItem("Split",  3);

    for (int i = 1; i <= 16; ++i)
    {
        _chACombo.addItem("Ch " + juce::String(i), i);
        _chBCombo.addItem("Ch " + juce::String(i), i);
    }

    styleCombo(_editTargetCombo);
    styleCombo(_chACombo);
    styleCombo(_chBCombo);
    styleCombo(_perfModeCombo);

    styleKnob(_voiceSplitKnob);
    styleKnob(_splitNoteKnob);
    styleKnob(_balanceKnob);

    addAndMakeVisible(_editTargetCombo);
    addAndMakeVisible(_chACombo);
    addAndMakeVisible(_chBCombo);
    addAndMakeVisible(_perfModeCombo);
    addAndMakeVisible(_voiceSplitKnob);
    addAndMakeVisible(_splitNoteKnob);
    addAndMakeVisible(_balanceKnob);

    // Bind each control to its APVTS parameter. JUCE handles the two-way
    // sync; our parameterChanged() in PluginProcessor still fires for the
    // atomic-mirror updates and the isLocalOnly skip-send logic.
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;

    _headerComboAtts.add (new ComboAtt (_proc.apvts, "perf_edit_target", _editTargetCombo));
    _headerComboAtts.add (new ComboAtt (_proc.apvts, "perf_midi_ch_a",   _chACombo));
    _headerComboAtts.add (new ComboAtt (_proc.apvts, "perf_midi_ch_b",   _chBCombo));
    _headerComboAtts.add (new ComboAtt (_proc.apvts, "perf_mode",        _perfModeCombo));
    _headerSliderAtts.add(new SliderAtt(_proc.apvts, "perf_voice_split", _voiceSplitKnob));
    _headerSliderAtts.add(new SliderAtt(_proc.apvts, "perf_split_note",  _splitNoteKnob));
    _headerSliderAtts.add(new SliderAtt(_proc.apvts, "perf_balance",     _balanceKnob));

    // --- MIDI output selector (ALL modes — standalone AND VST3) -------------
    // WHY: stock JUCE's VST3 wrapper drops outgoing SysEx from the processBlock
    // MIDI bus, so the host (Ableton) never forwards our SET_PARAM/BANK_DUMP to
    // the Teensy. Opening an OS MIDI output device here bypasses the host MIDI
    // path entirely, which is the only reliable SysEx route in VST3. The
    // selector is therefore visible in every wrapper type, not just standalone.
    _midiOutSelector.onChange = [this]
    {
        const int sel = _midiOutSelector.getSelectedId();
        if (sel <= 0) { _proc.setMidiOutput(nullptr); return; }
        auto devices = juce::MidiOutput::getAvailableDevices();
        const int idx = sel - 1;
        if (idx >= 0 && idx < devices.size())
            _proc.setMidiOutput(juce::MidiOutput::openDevice(devices[idx].identifier));
    };
    addAndMakeVisible(_midiOutSelector);
    _midiOutSelector.setVisible(true);   // was: _isStandalone

    // --- MIDI input checklist (standalone only) -----------------------------
    // Multi-select: the button opens a PopupMenu with one tickable row per
    // available input device. Ticking toggles that device in the processor's
    // open set. The Auto no-feedback rule (in the processor) makes whichever
    // device equals the MIDI-Out selection listen-only; all others forward.
    _midiInButton.onClick = [this] { showMidiInputMenu(); };
    addAndMakeVisible(_midiInButton);
    _midiInButton.setVisible(_isStandalone);

    // Output list is needed in every mode (see selector comment above).
    // The input menu is built on demand, so nothing to pre-populate here.
    refreshMidiOutputList();

    // --- Style helper for buttons -------------------------------------------
    auto styleBtn = [](juce::TextButton& b, juce::Colour c)
    {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff161c28));
        b.setColour(juce::TextButton::textColourOffId, c);
    };

    // --- Send All -----------------------------------------------------------
    // Phase 6: replaces the 90-CC paced spray with one BANK_DUMP. Step
    // sequencer values aren't part of the BANK_DUMP (they're transient
    // CC 17/18 pair edits), so we still send those via the new step-seq API.
    styleBtn(_sendAllBtn, kOrange);
    _sendAllBtn.onClick = [this]
    {
        _proc.sendBankDump();
        for (auto* sec : _sections)
        {
            for (auto* grid : sec->getStepGrids())
            {
                for (int s = 0; s < StepSequencerGrid::kNumSteps; ++s)
                {
                    _proc.sendStepSeqEdit(s, grid->getStepValue(s));
                }
            }
        }
    };

    // --- Load SYX -----------------------------------------------------------
    styleBtn(_loadSyxBtn, kPurple);
    _loadSyxBtn.onClick = [this] { loadSyxFile(); };

    // --- Save / Load patch --------------------------------------------------
    styleBtn(_savePatchBtn, kGreen);
    styleBtn(_loadPatchBtn, kGreen);
    _savePatchBtn.onClick = [this] { savePatchFile(); };
    _loadPatchBtn.onClick = [this] { loadPatchFile(); };

    // --- Init patch ---------------------------------------------------------
    styleBtn(_initPatchBtn, kGreen);
    _initPatchBtn.onClick = [this] { initPatch(); };

    // --- Expand / Collapse --------------------------------------------------
    styleBtn(_expandBtn, kText);
    styleBtn(_collapseBtn, kText);
    _expandBtn.onClick   = [this] { for (auto* s : _sections) s->setOpen(true); };
    _collapseBtn.onClick = [this] { for (auto* s : _sections) s->setOpen(false); };

    for (auto* b : { &_sendAllBtn, &_loadSyxBtn, &_savePatchBtn, &_loadPatchBtn,
                     &_initPatchBtn, &_expandBtn, &_collapseBtn })
        addAndMakeVisible(b);

    // --- Scrollable viewport ------------------------------------------------
    _viewport.setViewedComponent(&_contentPanel, false);
    _viewport.setScrollBarsShown(true, false);
    _viewport.getVerticalScrollBar().setColour(
        juce::ScrollBar::thumbColourId, juce::Colour(0xff283850));
    addAndMakeVisible(_viewport);

    buildSections();
    setSize(1080, 820);
    setResizable(true, true);
    setResizeLimits(400, 400, 1600, 2400);

    // Poll for MIDI device hot-plug in every mode. The timer refreshes the
    // output list always, and the input list only in standalone (see
    // timerCallback). Previously gated to standalone, which left the VST3
    // output selector stale when the Teensy was plugged in after load.
    startTimer(2000);
}

JT8000ControllerEditor::~JT8000ControllerEditor()
{
    stopTimer();
    _proc.apvts.state.removeListener(this);
    setLookAndFeel(nullptr);
}

// =============================================================================
// MIDI device selectors
//   refreshMidiOutputList — output ComboBox, all modes (always shown)
//   showMidiInputMenu     — input multi-select PopupMenu, standalone only
// =============================================================================
void JT8000ControllerEditor::refreshMidiOutputList()
{
    _midiOutSelector.clear(juce::dontSendNotification);
    _midiOutSelector.addItem("-- No MIDI Output --", -1);
    auto devices = juce::MidiOutput::getAvailableDevices();
    for (int i = 0; i < devices.size(); ++i)
        _midiOutSelector.addItem(devices[i].name, i + 1);
    _midiOutSelector.setSelectedId(-1, juce::dontSendNotification);
}

// =============================================================================
// showMidiInputMenu — tickable multi-select of available MIDI input devices.
//
// Built fresh on each open so hot-plugged devices appear without a timer.
// A ticked item means "currently open in the processor"; clicking it toggles
// that single device (open set is otherwise preserved).
// =============================================================================
void JT8000ControllerEditor::showMidiInputMenu()
{
    auto devices = juce::MidiInput::getAvailableDevices();

    juce::PopupMenu menu;
    if (devices.isEmpty())
    {
        menu.addItem(1, "(no MIDI inputs)", false, false);
    }
    else
    {
        for (int i = 0; i < devices.size(); ++i)
        {
            const bool ticked = _proc.isMidiInputOpen(devices[i].identifier);
            // itemId = i+1; enabled; ticked = currently open.
            menu.addItem(i + 1, devices[i].name, true, ticked);
        }
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&_midiInButton),
        [this, devices](int result)
        {
            if (result <= 0 || result > devices.size()) return;   // dismissed

            const auto toggledId = devices[result - 1].identifier;

            // Rebuild the desired open set = current open set XOR the toggled id.
            juce::StringArray desired;
            for (const auto& d : devices)
                if (_proc.isMidiInputOpen(d.identifier))
                    desired.add(d.identifier);

            if (desired.contains(toggledId)) desired.removeString(toggledId);
            else                             desired.add(toggledId);

            _proc.openMidiInputs(desired);
        });
}

// =============================================================================
// Timer — MIDI device hot-plug refresh (output: all modes, input: standalone)
// =============================================================================
void JT8000ControllerEditor::timerCallback()
{
    // Only the output ComboBox needs hot-plug polling — it shows a fixed list.
    // The input checklist is a PopupMenu rebuilt each time it opens, so it
    // always reflects current devices without polling. Runs in all modes
    // (the output selector exists in VST3 too).
    const int outSel = _midiOutSelector.getSelectedId();
    auto outDevices  = juce::MidiOutput::getAvailableDevices();

    if (outDevices.size() + 1 != _midiOutSelector.getNumItems())
    {
        refreshMidiOutputList();
        if (outSel > 0 && outSel <= outDevices.size())
            _midiOutSelector.setSelectedId(outSel, juce::dontSendNotification);
    }
}

// =============================================================================
// Build sections
//
// Creates a SectionComponent for every Section enum value EXCEPT
// PERFORMANCE_GLOBAL — that pseudo-section's params (CCs 140..146) are
// rendered directly in the editor header, not inside a collapsible panel.
// We still reserve an array slot for it so integer casts from Section →
// int stay valid elsewhere; the slot's SectionComponent is created but
// never made visible and no params are added to it.
// =============================================================================
void JT8000ControllerEditor::buildSections()
{
    for (int s = 0; s < static_cast<int>(Section::NUM_SECTIONS); ++s)
    {
        auto* sec = _sections.add(
            new SectionComponent(sectionNames[s], juce::Colour(0xfff08020u), _lnf));
        sec->onLayoutChanged = [this] { layoutSections(); };

        // PERFORMANCE_GLOBAL is header-only — don't show its panel.
        if (s == static_cast<int>(Section::PERFORMANCE_GLOBAL))
            continue;

        _contentPanel.addAndMakeVisible(sec);
    }

    // Add visual envelope displays to their sections FIRST so they appear
    // at the top, with the knobs flowing below for fine control.
    _sections[static_cast<int>(Section::AMP_ENV)]
        ->addEnvelope(_proc.apvts, JuceEnvFlavour::AMP);
    _sections[static_cast<int>(Section::FILTER_ENV)]
        ->addEnvelope(_proc.apvts, JuceEnvFlavour::FILTER);
    _sections[static_cast<int>(Section::PITCH_ENV)]
        ->addEnvelope(_proc.apvts, JuceEnvFlavour::PITCH);

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];

        // Perf-global params are bound directly to header controls in the
        // editor constructor (see _editTargetCombo, _chACombo, etc.).
        // Skip them here so they are NOT added to a SectionComponent —
        // that would bind them twice and cause feedback / duplicate UI.
        if (p.section == Section::PERFORMANCE_GLOBAL)
            continue;

        auto* sec = _sections[static_cast<int>(p.section)];
        switch (p.style)
        {
            case ControlStyle::Knob:   sec->addKnob(_proc.apvts, p);     break;
            case ControlStyle::Slider: sec->addSlider(_proc.apvts, p);   break;
            case ControlStyle::Combo:  sec->addComboBox(_proc.apvts, p); break;
            case ControlStyle::Toggle: sec->addToggle(_proc.apvts, p);   break;
            case ControlStyle::StepGrid:
                sec->addStepGrid([this](int step, int value) {
                    _proc.sendStepSeqEdit(step, value);
                });
                break;
        }
    }
}

// =============================================================================
// Layout sections
//
// Walks _sections in order, placing them into the content panel according
// to sectionRow[]. Groups sharing a row number get equal-width side-by-side
// placement.
//
// PERFORMANCE_GLOBAL is skipped: its SectionComponent slot exists but is
// never made visible (see buildSections), so it must not occupy vertical
// space in the scrolling panel.
// =============================================================================
void JT8000ControllerEditor::layoutSections()
{
    const int scrollBarW = _viewport.getScrollBarThickness();
    const int totalW = _viewport.getWidth() - scrollBarW - 8;
    const int gap = 4;
    if (totalW <= 0) return;

    int y = gap, idx = 0;
    const int n = _sections.size();
    const int globalIdx = static_cast<int>(Section::PERFORMANCE_GLOBAL);

    while (idx < n)
    {
        // Skip the pseudo-section that lives in the header, not on the panel.
        if (idx == globalIdx) { ++idx; continue; }

        const int row = sectionRow[idx];
        if (row < 0)
        {
            auto* sec = _sections[idx];
            int h = sec->getDesiredHeight(totalW);
            sec->setBounds(gap, y, totalW, h);
            y += h + gap;
            ++idx;
        }
        else
        {
            int gs = idx;
            while (idx < n && sectionRow[idx] == row && idx != globalIdx) ++idx;
            int gc = idx - gs;
            int sw = (totalW - (gc - 1) * gap) / gc;
            int maxH = 0;
            for (int g = 0; g < gc; ++g)
                maxH = juce::jmax(maxH, _sections[gs + g]->getDesiredHeight(sw));
            int xp = gap;
            for (int g = 0; g < gc; ++g)
            {
                _sections[gs + g]->setBounds(xp, y, sw, maxH);
                xp += sw + gap;
            }
            y += maxH + gap;
        }
    }
    _contentPanel.setSize(totalW + 2 * gap, y + gap);
}

// =============================================================================
// Paint
// =============================================================================
void JT8000ControllerEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    g.setColour(kHeaderBg);
    g.fillRect(0, 0, getWidth(), kHeaderHeight);
    g.setColour(kBorder);
    g.drawHorizontalLine(kHeaderHeight - 1, 0.0f, static_cast<float>(getWidth()));

    // Three-row header: two internal dividers at 1/3 and 2/3 of header height.
    // Faded border colour matches the pre-Phase-2 look where the single
    // divider sat at kHeaderHeight / 2.
    g.setColour(kBorder.withAlpha(0.4f));
    const int row1Div = kHeaderHeight / 3;
    const int row2Div = (kHeaderHeight * 2) / 3;
    g.drawHorizontalLine(row1Div, 6.0f, static_cast<float>(getWidth() - 6));
    g.drawHorizontalLine(row2Div, 6.0f, static_cast<float>(getWidth() - 6));
}

// =============================================================================
// Resized — three-row header layout
//
//   Row 1: Title | PatchName | (MIDI In | MIDI Out)*
//                                               * standalone only
//   Row 2: SendAll | LoadSYX | Save | Load | Init | … | Expand | Collapse
//   Row 3: [Layers:] EditTarget | ChA | ChB    [Perf:] Mode | VoiceSpl | Split | Bal
//
// Row 3 is the Phase-2 addition. The single MIDI-channel combo that used
// to live in row 1 is gone — per-layer channels live in row 3 instead.
// =============================================================================
void JT8000ControllerEditor::resized()
{
    auto area = getLocalBounds();
    auto hdrFull = area.removeFromTop(kHeaderHeight);

    const int rowH = kHeaderHeight / 3;   // was /2 before Phase 2
    const int pad  = 4;

    // ---- Row 1 — title + patch name + MIDI IO --------------------------------
    auto row1 = hdrFull.removeFromTop(rowH).reduced(pad, 2);

    _titleLabel.setBounds(row1.removeFromLeft(72));
    row1.removeFromLeft(pad);

    // MIDI Out is present in all modes; MIDI In only in standalone. Size each
    // box to ~1/3 of the row so the patch-name LCD keeps the remainder.
    {
        const int midiBoxW = juce::jmin(160, (row1.getWidth() - pad) / 3);

        if (_isStandalone)
        {
            _midiInButton.setBounds(row1.removeFromRight(midiBoxW));
            row1.removeFromRight(pad);
        }

        _midiOutSelector.setBounds(row1.removeFromRight(midiBoxW));
        row1.removeFromRight(pad);
    }

    _patchNameLabel.setBounds(row1);

    // ---- Row 2 — action buttons ---------------------------------------------
    auto row2 = hdrFull.removeFromTop(rowH).reduced(pad, 2);
    _sendAllBtn.setBounds(row2.removeFromLeft(66));
    row2.removeFromLeft(pad);
    _loadSyxBtn.setBounds(row2.removeFromLeft(72));
    row2.removeFromLeft(pad);
    _savePatchBtn.setBounds(row2.removeFromLeft(50));
    row2.removeFromLeft(pad);
    _loadPatchBtn.setBounds(row2.removeFromLeft(50));
    row2.removeFromLeft(pad);
    _initPatchBtn.setBounds(row2.removeFromLeft(44));

    _collapseBtn.setBounds(row2.removeFromRight(66));
    row2.removeFromRight(pad);
    _expandBtn.setBounds(row2.removeFromRight(60));

    // ---- Row 3 — Layers + Perf Global (Phase 2) -----------------------------
    // Left block: "Layers:" | EditTarget | ChA | ChB
    // Right block: "Perf:" | Mode | Voice | Split | Balance
    // Middle space is flexible — collapses first when window is narrow.
    auto row3 = hdrFull.reduced(pad, 2);

    // Label widths — make them just wide enough for the text
    constexpr int labelW  = 52;
    constexpr int comboW  = 70;   // Edit Target, Ch A/B, Perf Mode
    constexpr int knobW   = 48;   // small header knobs

    // Left block
    _perfLabel.setBounds(row3.removeFromLeft(labelW));
    row3.removeFromLeft(pad);
    _editTargetCombo.setBounds(row3.removeFromLeft(comboW));
    row3.removeFromLeft(pad);
    _chACombo.setBounds(row3.removeFromLeft(comboW));
    row3.removeFromLeft(pad);
    _chBCombo.setBounds(row3.removeFromLeft(comboW));

    // Right block — lay out from the right so labels/knobs anchor cleanly
    _balanceKnob.setBounds(row3.removeFromRight(knobW));
    row3.removeFromRight(pad);
    _splitNoteKnob.setBounds(row3.removeFromRight(knobW));
    row3.removeFromRight(pad);
    _voiceSplitKnob.setBounds(row3.removeFromRight(knobW));
    row3.removeFromRight(pad);
    _perfModeCombo.setBounds(row3.removeFromRight(comboW));
    row3.removeFromRight(pad);
    _perfSetLabel.setBounds(row3.removeFromRight(labelW));

    _viewport.setBounds(area);
    layoutSections();

    if (_syxOverlay != nullptr)
        _syxOverlay->setBounds(getLocalBounds());
}

// =============================================================================
// ValueTree::Listener
// =============================================================================
void JT8000ControllerEditor::valueTreePropertyChanged(
    juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == kPatchNameProp)
    {
        const auto name = tree.getProperty(property).toString();
        juce::MessageManager::callAsync([this, name]
        {
            _patchNameLabel.setText(name, juce::dontSendNotification);
        });
    }
}

// =============================================================================
// Init Patch  (Phase 6: BANK_DUMP path)
//
// Resets every patch parameter to its default value. Specifically PRESERVES
// the seven perf-global params (CCs 140..146 — channels, edit target, perf
// mode, balance, split) because those describe the current performance
// setup, not the patch being initialised.
//
// Phase 6: each setValueNotifyingHost() call would normally fire its own
// SET_PARAM SysEx via parameterChanged. We suppress that with _suppressEcho
// during the bulk reset, then send one BANK_DUMP at the end — atomic,
// no audible knob-sweep.
// =============================================================================
void JT8000ControllerEditor::initPatch()
{
    _proc._suppressEcho.store(true);

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];
        if (p.style == ControlStyle::StepGrid) continue;
        if (p.isLocalOnly) continue;   // keep perf-global state intact
        if (auto* param = _proc.apvts.getParameter(p.id))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(p.defVal)));
    }

    _proc.setPatchName("INIT PATCH");
    _proc._suppressEcho.store(false);

    // Flush the new state in one BANK_DUMP.
    _proc.sendBankDump();

    // Reset step sequencer values + push them to the firmware.
    for (auto* sec : _sections)
    {
        for (auto* grid : sec->getStepGrids())
        {
            for (int s = 0; s < StepSequencerGrid::kNumSteps; ++s)
            {
                grid->setStepValue(s, 0, false);
                _proc.sendStepSeqEdit(s, 0);
            }
        }
    }
}

// =============================================================================
// SYX import — non-modal overlay (safe in all VST3 hosts)
//
// Two-phase flow:
//   1. loadSyxFile() — opens a file chooser, parses the file into a
//      ParsedBank containing performances (with upper/lower patches) and
//      orphan patches (those without a performance header).
//   2. showBankPicker() — displays the SyxPickerOverlay with performance-
//      aware load options (Perf→A+B, Upper→A, Lower→B, etc.)
// =============================================================================
void JT8000ControllerEditor::loadSyxFile()
{
    _fileChooser = std::make_unique<juce::FileChooser>(
        "Load JP-8000 SysEx",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.syx;*.mid;*.SYX;*.MID");

    _fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto results = fc.getResults();
            if (results.isEmpty()) return;

            auto file = results.getFirst();
            juce::MemoryBlock mb;
            if (!file.loadFileAsData(mb) || mb.getSize() == 0) return;

            _syxBuffer = std::move(mb);
            _syxBank = JP8000SyxImporter::parseFile(
                static_cast<const uint8_t*>(_syxBuffer.getData()),
                _syxBuffer.getSize(),
                file.getFileExtension().toLowerCase() == ".mid");

            if (_syxBank.performances.empty() && _syxBank.orphanPatches.empty())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "No Patches Found",
                    "No JP-8000 patches found in " + file.getFileName());
                return;
            }
            showBankPicker(file.getFileName());
        });
}

void JT8000ControllerEditor::showBankPicker(const juce::String& fileName)
{
    const int nPerfs   = static_cast<int>(_syxBank.performances.size());
    const int nOrphans = static_cast<int>(_syxBank.orphanPatches.size());

    juce::String title = "Load SysEx -- " + fileName + " -- ";
    if (nPerfs > 0)
        title += juce::String(nPerfs) + " perf" + (nPerfs > 1 ? "s" : "");
    if (nPerfs > 0 && nOrphans > 0)
        title += ", ";
    if (nOrphans > 0)
        title += juce::String(nOrphans) + " patch" + (nOrphans > 1 ? "es" : "");

    _syxOverlay = std::make_unique<SyxPickerOverlay>(title, _syxBank);
    _syxOverlay->setBounds(getLocalBounds());

    _syxOverlay->onResult = [this](PickAction action, int perfIdx, int patchIdx)
    {
        using namespace JP8000SyxImporter;

        switch (action)
        {
            // ---- Performance actions ----
            case PickAction::PerfToAB:
            {
                if (perfIdx >= 0 && perfIdx < static_cast<int>(_syxBank.performances.size()))
                {
                    const auto& perf = _syxBank.performances[static_cast<size_t>(perfIdx)];
                    loadPerformance(_proc, perf);
                }
                break;
            }
            case PickAction::UpperToA:
            {
                if (perfIdx >= 0 && perfIdx < static_cast<int>(_syxBank.performances.size()))
                {
                    const auto& perf = _syxBank.performances[static_cast<size_t>(perfIdx)];
                    loadPatchToLayer(_proc, perf.upper, LoadTarget::LayerA);
                }
                break;
            }
            case PickAction::UpperToB:
            {
                if (perfIdx >= 0 && perfIdx < static_cast<int>(_syxBank.performances.size()))
                {
                    const auto& perf = _syxBank.performances[static_cast<size_t>(perfIdx)];
                    loadPatchToLayer(_proc, perf.upper, LoadTarget::LayerB);
                }
                break;
            }
            case PickAction::LowerToA:
            {
                if (perfIdx >= 0 && perfIdx < static_cast<int>(_syxBank.performances.size()))
                {
                    const auto& perf = _syxBank.performances[static_cast<size_t>(perfIdx)];
                    loadPatchToLayer(_proc, perf.lower, LoadTarget::LayerA);
                }
                break;
            }
            case PickAction::LowerToB:
            {
                if (perfIdx >= 0 && perfIdx < static_cast<int>(_syxBank.performances.size()))
                {
                    const auto& perf = _syxBank.performances[static_cast<size_t>(perfIdx)];
                    loadPatchToLayer(_proc, perf.lower, LoadTarget::LayerB);
                }
                break;
            }

            // ---- Orphan patch actions ----
            case PickAction::PatchToA:
            {
                if (patchIdx >= 0 && patchIdx < static_cast<int>(_syxBank.orphanPatches.size()))
                {
                    const auto& patch = _syxBank.orphanPatches[static_cast<size_t>(patchIdx)];
                    loadPatchToLayer(_proc, patch, LoadTarget::LayerA);
                }
                break;
            }
            case PickAction::PatchToB:
            {
                if (patchIdx >= 0 && patchIdx < static_cast<int>(_syxBank.orphanPatches.size()))
                {
                    const auto& patch = _syxBank.orphanPatches[static_cast<size_t>(patchIdx)];
                    loadPatchToLayer(_proc, patch, LoadTarget::LayerB);
                }
                break;
            }
            case PickAction::PatchSend:
            {
                if (patchIdx >= 0 && patchIdx < static_cast<int>(_syxBank.orphanPatches.size()))
                {
                    const auto& patch = _syxBank.orphanPatches[static_cast<size_t>(patchIdx)];
                    loadPatchToLayer(_proc, patch, LoadTarget::Current);
                }
                break;
            }
            case PickAction::PatchOnly:
            {
                if (patchIdx >= 0 && patchIdx < static_cast<int>(_syxBank.orphanPatches.size()))
                {
                    const auto& patch = _syxBank.orphanPatches[static_cast<size_t>(patchIdx)];
                    loadPatch(_proc.apvts, patch);
                    _proc.setPatchName(patch.name);
                    // No send — user explicitly chose "Load Only"
                }
                break;
            }

            case PickAction::Cancel:
            default:
                break;
        }

        _syxOverlay.reset();
    };

    addAndMakeVisible(_syxOverlay.get());
}

// =============================================================================
// Patch file save / load
// =============================================================================
void JT8000ControllerEditor::savePatchFile()
{
    _fileChooser = std::make_unique<juce::FileChooser>(
        "Save JT-8000 Patch",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.jt8k");

    _fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto results = fc.getResults();
            if (results.isEmpty()) return;

            auto file = results.getFirst();
            if (!file.hasFileExtension("jt8k"))
                file = file.withFileExtension("jt8k");

            _proc.setPatchName(file.getFileNameWithoutExtension());

            auto xml = _proc.apvts.copyState().createXml();
            if (xml && !xml->writeTo(file))
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Save Failed", "Could not write to " + file.getFileName());
        });
}

void JT8000ControllerEditor::loadPatchFile()
{
    _fileChooser = std::make_unique<juce::FileChooser>(
        "Load JT-8000 Patch",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.jt8k");

    _fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto results = fc.getResults();
            if (results.isEmpty()) return;

            auto file = results.getFirst();
            auto xml = juce::XmlDocument::parse(file);

            if (!xml)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Load Failed", "Could not parse " + file.getFileName());
                return;
            }
            if (!xml->hasTagName(_proc.apvts.state.getType()))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Invalid File",
                    file.getFileName() + " is not a valid JT-8000 patch.");
                return;
            }

            _proc.apvts.replaceState(juce::ValueTree::fromXml(*xml));

            if (_proc.getPatchName().isEmpty())
                _proc.setPatchName(file.getFileNameWithoutExtension());

            // Phase 6: replaceState updated APVTS but didn't trigger
            // parameterChanged callbacks. Send the new state to the
            // firmware as one BANK_DUMP.
            _proc.sendBankDump();
        });
}