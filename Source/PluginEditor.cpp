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

        /* The LCD shows the patch name too, and nothing else pushes it there -
         * editing the field silently left the two disagreeing. */
        if (_panel != nullptr)
            _panel->setPatchName(_patchNameLabel.getText());
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

    // Populate combo items from the CCDefinitions row itself (Phase J1):
    // hand-typed duplicates of the option lists were how the header could
    // drift from the table — now the table IS the item source, so a row
    // change (like D-J5's new selects) can never desynchronise the UI.
    // (JUCE ComboBox::addItem IDs are 1-based; the attachment maps
    //  index 0..N-1 to combo IDs 1..N.)
    auto fillFromRow = [](juce::ComboBox& cb, const char* paramId)
    {
        if (const CCParam* p = findParamByID(paramId))
            for (int i = 0; i < p->enumCount; ++i)
                cb.addItem(p->enumNames[i], i + 1);
    };




    // ---- Embedded engine controls (Phase J2) -------------------------------
    // Present only when the build carries the engine; a controller-only
    // build shows exactly the pre-J2 header.
    if (_proc.engineAvailable())
    {
        _engineModeCombo.addItem("Controller", 1);
        _engineModeCombo.addItem("Sound",      2);
        _engineModeCombo.addItem("Both",       3);
        styleCombo(_engineModeCombo);
        _engineModeCombo.setSelectedItemIndex(_proc.engineMode(),
                                              juce::dontSendNotification);
        _engineModeCombo.onChange = [this]
        {
            _proc.setEngineMode(_engineModeCombo.getSelectedItemIndex());
            applyEngineSampleRatePolicy();
        };
        addAndMakeVisible(_engineModeCombo);

    }

    // Bind each control to its APVTS parameter. JUCE handles the two-way
    // sync; our parameterChanged() in PluginProcessor still fires for the
    // atomic-mirror updates and the isLocalOnly skip-send logic.
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;


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


    /* Send All is now a skin header button, routed through _panel->onAction to
     * this method. The step grids it used to walk belonged to SectionComponent;
     * the panel writes through the APVTS, so a bank dump carries everything.
     *
     * NOT VERIFIED: if the firmware needs the step select/value pairs re-sent
     * explicitly after a dump, that belongs in the processor rather than here. */



    /* The RmlUi panel. Assets are registered and the document loaded inside
     * JtRmlPanel; the editor only shows it and gives it bounds. */
    _panel = std::make_unique<jt::JtRmlPanel>(_proc.apvts);
    addAndMakeVisible(*_panel);
    _panel->setPatchName(_proc.getPatchName());

    /* The skin's header buttons drive the actions this editor already
     * implements - they are routed, not reimplemented. */
    _panel->onAction = [this](const juce::String& a)
    {
        if      (a == "sendall") _proc.sendBankDump();
        else if (a == "loadsyx") loadSyxFile();
        else if (a == "save")    savePatchFile();
        else if (a == "load")    loadPatchFile();
        else if (a == "init")    initPatch();
    };

    /* Authored at 3840x2160 and correct at half that. At the old 1080px width
     * a knob would be 40px, too small to use. */
    setSize(jt::JtRmlPanel::kNativeW,
            jt::JtRmlPanel::kNativeH + kHeaderHeight);
    setResizable(true, true);
    setResizeLimits(1280, 720 + kHeaderHeight, 3840, 2160 + kHeaderHeight);
    setResizable(true, true);
    setResizeLimits(400, 400, 1600, 2400);

    // One timer, two cadences (Phase J1d): every tick services the engine
    // status feed (voice dots + grid playhead — cheap, change-driven), and
    // every kDevicePollTicks-th tick runs the MIDI device hot-plug scan at
    // its original 2 s rate (device enumeration is NOT cheap on some OSes,
    // so it must not ride the fast tick).
    startTimer(kStatusPollMs);
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

    // Restore the SHOWN selection from the processor's currently-open device.
    // The processor keeps directMidiOut open across editor lifetimes, so on a
    // rebuild (host reopening the window) the combo must reflect what is
    // already selected — otherwise it reads "No MIDI Output" while control
    // traffic still flows out the direct device.  Match by identifier, not
    // name, since names can repeat.  Fall back to -1 when nothing is open.
    const juce::String openId = _proc.getDirectMidiOutId();
    int shownId = -1;
    if (openId.isNotEmpty())
        for (int i = 0; i < devices.size(); ++i)
            if (devices[i].identifier == openId) { shownId = i + 1; break; }

    _midiOutSelector.setSelectedId(shownId, juce::dontSendNotification);
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
// =============================================================================
// applyEngineSampleRatePolicy (Phase J2, sign-off Q4 — fallback variant)
//
// The auto-request originally reached the standalone's device manager via
// juce::StandalonePluginHolder; this JUCE version does not export that
// class through juce_audio_utils (MSVC C2039, 2026-07-20), and the flagged
// fallback applies: the orange rate label carries the whole policy.  Set
// 44 100 Hz once in Options -> Audio Settings — the standalone persists it.
// Ledgered: revisit if a public device-manager hook appears, alongside the
// proper-resampling item.
// =============================================================================
void JT8000ControllerEditor::applyEngineSampleRatePolicy()
{
    // timerCallback() is the active off-rate signal on every platform.
}

void JT8000ControllerEditor::timerCallback()
{
    // ---- Fast lane: engine status feed (J1d) --------------------------------
    // Liveness first: the counter moves once per received status cluster,
    // heartbeat included, so "count moved" == "wire alive" even when the
    // word itself never changes.
    const uint32_t count = _proc.statusCount();
    if (count != _lastStatusCount) { _lastStatusCount = count; _ticksSinceBeat = 0; }
    else if (_ticksSinceBeat < kLinkStaleTicks) ++_ticksSinceBeat;

    const bool     alive = _ticksSinceBeat < kLinkStaleTicks;
    const uint16_t word  = _proc.statusWord();

    // Both sinks are change-driven internally, so feeding them every tick
    // costs a couple of integer compares when nothing moved.
    /* Voice activity moved into the panel's LCD - same mask, same heartbeat. */
    if (_panel != nullptr)
        _panel->setVoiceActivity(ValueLaw::statusVoiceMask(word), alive);

    /* Rate warning dropped: it was a JUCE label with no panel equivalent. */

    const bool running = alive && ValueLaw::statusRunning(word);
    const int  step    = (int) ValueLaw::statusPlayStep(word);

    /* One call: the panel forwards the playhead to every step grid it owns. */
    if (_panel != nullptr)
        _panel->setPlayhead(running ? step : -1);

    // ---- Slow lane: MIDI device hot-plug scan (original 2 s cadence) --------
    // Only the output ComboBox needs polling — it shows a fixed list.  The
    // input checklist is a PopupMenu rebuilt each time it opens, so it always
    // reflects current devices without polling.  Runs in all modes (the
    // output selector exists in VST3 too).  Device enumeration can be
    // expensive OS-side; it must never ride the 50 ms tick.
    if (++_pollTick < kDevicePollTicks)
        return;
    _pollTick = 0;

    auto outDevices  = juce::MidiOutput::getAvailableDevices();

    // Rebuild only when the device set changed (hot-plug).  The restore is
    // handled inside refreshMidiOutputList BY IDENTIFIER, which — unlike the
    // old index-based reselect — survives a device being added or removed
    // ahead of the open one in the list.
    if (outDevices.size() + 1 != _midiOutSelector.getNumItems())
        refreshMidiOutputList();
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

    /* Single strip now, so the two internal dividers are gone with the rows
     * they separated. */
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
    /* One 34px strip, not three rows. Everything else moved into the panel:
     * row 3 was entirely parameters (now the Setup layer) and row 2's buttons
     * are painted into the skin's header. What remains cannot come from the
     * document - MIDI ports are discovered at runtime. */
    auto area = getLocalBounds();
    auto strip = area.removeFromTop(kHeaderHeight).reduced(6, 3);

    constexpr int pad = 6;
    const int boxW = juce::jmin(180, (strip.getWidth() - pad * 3) / 4);

    _midiInButton.setBounds(strip.removeFromRight(boxW));
    strip.removeFromRight(pad);
    _midiOutSelector.setBounds(strip.removeFromRight(boxW));
    strip.removeFromRight(pad);
    _engineModeCombo.setBounds(strip.removeFromRight(110));
    strip.removeFromRight(pad);

    /* The patch name is also shown on the panel's LCD; this field is what makes
     * it editable until the LCD accepts text entry. */
    _patchNameLabel.setBounds(strip);

    _panel->setBounds(area);

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
    if (_panel != nullptr)
        _panel->setPatchName("INIT PATCH");
    _proc._suppressEcho.store(false);

    // Flush the new state in one BANK_DUMP.
    _proc.sendBankDump();

    /* The step lanes were reset above with every other parameter, and the
     * panel reads them back on the next sync - the grids this used to walk
     * belonged to SectionComponent. */
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
                    if (_panel != nullptr)
                        _panel->setPatchName(patch.name);
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