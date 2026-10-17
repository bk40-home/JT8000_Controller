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
#include "gen/JtParamTable.h"   // JtParams::kParams — the same generated table
                               // the processor builds its APVTS from.

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

    // --- Patch-name editor: a TRANSPARENT overlay on the LCD's top line ------
    // The name used to show twice (this field AND the LCD). Now the LCD is the
    // only place it appears: this label sits over the LCD line, invisible until
    // you double-click it to rename, then shows a readable editor. It becomes a
    // child of the panel below (so it scrolls/scales with the LCD) and is placed
    // by positionLcdEditor().
    _patchNameLabel.setText(_proc.getPatchName(), juce::dontSendNotification);
    _patchNameLabel.setColour(juce::Label::textColourId,       juce::Colours::transparentBlack);
    _patchNameLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    _patchNameLabel.setColour(juce::Label::outlineColourId,    juce::Colours::transparentBlack);
    // Readable only while editing.
    _patchNameLabel.setColour(juce::Label::textWhenEditingColourId,       kLcdText);
    _patchNameLabel.setColour(juce::Label::backgroundWhenEditingColourId, kLcdBg);
    _patchNameLabel.setJustificationType(juce::Justification::centredLeft);
    _patchNameLabel.setEditable(false, true, false);   // double-click to rename
    _patchNameLabel.onTextChange = [this]
    {
        _proc.setPatchName(_patchNameLabel.getText());
        if (_panel != nullptr)
            _panel->setPatchName(_patchNameLabel.getText());
    };
    // NOTE: added to the panel (not the editor) after the panel is created.

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

    /* Load a skin folder at runtime (an exported jt8000.rml + the existing
     * rcss/PNGs). Always visible: it does not depend on the loaded document. */
    _loadSkinButton.onClick = [this] { loadSkinFolder(); };
    addAndMakeVisible(_loadSkinButton);

    /* Return to the embedded factory skin: clears the disk-root and document
     * name and reloads. */
    _factorySkinButton.onClick = [this]
    {
        if (_panel != nullptr)
            _panel->resetSkin();
        _proc.setSkinPath({});   // back to factory: clear persisted skin
    };
    addAndMakeVisible(_factorySkinButton);

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
    /* The panel goes inside a scrollable viewport (see PluginEditor.h). The
     * viewport does NOT own the panel — the unique_ptr does — so pass false. */
    _viewport.setViewedComponent(_panel.get(), false);
    _viewport.setScrollBarsShown(true, true);
    addAndMakeVisible(_viewport);
    _panel->setPatchName(_proc.getPatchName());

    /* Computer-keyboard note entry (standalone). The component itself is not
     * shown - it exists only for its built-in typing-to-note key mapping, which
     * it applies to the processor's shared MidiKeyboardState. Only wired up when
     * running standalone (a DAW provides its own note input). */
    /* Computer-keyboard note entry (standalone). Keys map directly to the
     * processor's shared MidiKeyboardState in keyPressed/keyStateChanged - no
     * MidiKeyboardComponent, so no extra GUI module dependency. */
    if (_isStandalone)
        setWantsKeyboardFocus(true);

    /* Restore the previously-chosen skin so a recreated editor (Ableton track
     * switch, window reopen) keeps it instead of reverting to factory. Empty or
     * a now-missing file falls back to the embedded factory. */
    {
        const juce::String skinPath = _proc.getSkinPath();
        if (skinPath.isNotEmpty())
        {
            const juce::File rml(skinPath);
            if (rml.existsAsFile())
                _panel->loadSkinFile(rml);
        }
    }

    // --- LCD parameter menu + simulated encoder -----------------------------
    // The menu walks the same generated table the APVTS is built from and edits
    // through the APVTS, so it cannot disagree with the panel or the engine.
    // It is the ONLY writer to the LCD: the panel's hover/knob readout is
    // diverted into showTransient() (shown only while the menu is idle), and
    // the menu's own output is written back through setLcd(). One writer, no
    // fight over the two lines.
    _menu = std::make_unique<jt::JtLcdMenu>(_proc.apvts);
    _menu->onLcd = [this](const juce::String& l1, const juce::String& l2)
    {
        if (_panel != nullptr)
            _panel->setLcd(l1, l2);
    };
    _panel->onLcdText = [this](const juce::String& l1, const juce::String& l2)
    {
        _menu->showTransient(l1, l2);
    };
    _menu->setPatchName(_proc.getPatchName());   // seed the idle top line

    // The simulated encoder maps 1:1 onto the menu's three primitives, so a
    // real hardware encoder would wire in identically later.
    _encoderStrip.onEncoder = [this](int delta, bool coarse)
    {
        _menu->encoder(delta, coarse);
    };
    _encoderStrip.onEnter = [this] { _menu->enter(); };
    _encoderStrip.onBack  = [this] { _menu->back();  };
    _encoderStrip.setKnobImage(_panel->knobImage());   // same art as the panel knobs
    /* Child of the PANEL, not the editor, so it scrolls and scales with the LCD
     * when the viewport is zoomed/panned. Added after the panel's inner RML
     * component, so it paints on top. */
    _panel->addAndMakeVisible(_encoderStrip);

    // Patch-name rename overlay lives on the LCD (see its setup above).
    _panel->addAndMakeVisible(_patchNameLabel);

    // Standalone-only zoom controls; the plugin window is host-sized.
    if (_isStandalone)
    {
        _zoomOutBtn.onClick = [this] { setZoom(_zoom / 1.15); };
        _zoomInBtn.onClick  = [this] { setZoom(_zoom * 1.15); };
        addAndMakeVisible(_zoomOutBtn);
        addAndMakeVisible(_zoomInBtn);
    }

    /* The skin's header buttons drive the actions this editor already
     * implements - they are routed, not reimplemented. */
    _panel->onAction = [this](const juce::String& a)
    {
        if      (a == "sendall") _proc.sendBankDump();
        else if (a == "loadsyx") showLibrary();
        else if (a == "save")    savePatchFile();
        else if (a == "load")    loadPatchFile();
        else if (a == "init")    initPatch();
        // Bass sequencer (ARP page while bass.enable is On).
        else if (a == "bass_new")    _proc.bassNewSeed();
        else if (a == "bass_mutate") _proc.bassMutate();
        else if (a == "bass_undo")   _proc.bassUndo();
        // Pattern edits (CLEAR / FREEZE / REVERT) live in the panel, which
        // owns the resolved pattern the FREEZE copies.
        else if (a.startsWith("bass_")) _panel->bassPatternAction(a);
    };

    /* Authored at 3840x2160, correct at half (1920x1080). But that can exceed a
     * scaled desktop (e.g. 3840x2160 @ 250% = 1536x864 usable), which would push
     * the resize edges off-screen. So open at native size but CLAMPED to the
     * available screen area, keeping the panel's aspect. The user can then
     * resize up to native within the limits below. */
    setResizable(true, true);
    setResizeLimits(400, 400 + kHeaderHeight,
                    jt::JtRmlPanel::kNativeW, jt::JtRmlPanel::kNativeH + kHeaderHeight);

    int wantW = jt::JtRmlPanel::kNativeW;
    int wantH = jt::JtRmlPanel::kNativeH + kHeaderHeight;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;               // already in scaled units
        const double margin = 0.92;                          // leave room for taskbar/title
        const double sx = (area.getWidth()  * margin) / (double) wantW;
        const double sy = (area.getHeight() * margin) / (double) wantH;
        const double s  = juce::jmin(1.0, sx, sy);           // only shrink, never grow
        wantW = juce::roundToInt(wantW * s);
        wantH = juce::roundToInt(wantH * s);
    }
    setSize(wantW, wantH);

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

    /* Break the LCD wiring before any member is torn down: the panel's binding
     * outlives _menu (declaration order), so leaving onLcdText pointing at a
     * destroyed menu would be a dangling call if a readout fired during
     * teardown. */
    if (_panel != nullptr)
        _panel->onLcdText = nullptr;

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

    /* Bass grid playhead: from the embedded engine only (decision 7b), so it
     * is -1 in Controller mode.  Word: [5..1] step, [0] running. */
    if (_panel != nullptr)
    {
        const uint16_t bw = _proc.bassStatusWord();
        _panel->setBassPlayhead((bw & 1u) ? (int) ((bw >> 1) & 0x1Fu) : -1);
    }

    /* Keep the encoder strip pinned to the LCD. Idempotent, so this settles it
     * once the skin's first layout pass has produced real element bounds (and
     * re-settles it after a skin reload) without costing anything when static. */
    positionEncoderStrip();

    /* Keep the menu's value line honest: while browsing/editing, the selected
     * parameter can change from outside the menu (a panel knob, automation, an
     * incoming NRPN). This re-reads it; no-op while idle, and the panel skips
     * the RML write when the text has not changed, so it is free when static. */
    if (_menu != nullptr)
        _menu->refresh();

    positionLcdEditor();   // settle the rename overlay once the skin has laid out

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

    /* Runtime skin loader - small fixed-width slot, always shown. */
    _loadSkinButton.setBounds(strip.removeFromRight(72));
    strip.removeFromRight(pad);
    _factorySkinButton.setBounds(strip.removeFromRight(72));
    strip.removeFromRight(pad);

    /* Standalone zoom controls, far left of the strip (the plugin window is
     * host-sized, so zoom is standalone-only). */
    if (_isStandalone)
    {
        _zoomOutBtn.setBounds(strip.removeFromLeft(30));
        strip.removeFromLeft(4);
        _zoomInBtn.setBounds(strip.removeFromLeft(30));
        strip.removeFromLeft(pad);
    }

    /* The patch name now lives on the LCD (a transparent double-click editor,
     * positioned by positionLcdEditor); nothing occupies the header filler. */

    area.removeFromTop(8);       // small gap so the panel top doesn't touch the header
    _viewport.setBounds(area);
    layoutPanel();               // sizes the panel for the zoom, repositions overlays

    if (_libraryBrowser != nullptr)
        _libraryBrowser->setBounds(getLocalBounds());
}

// =============================================================================
// Panel sizing inside the viewport.
//   zoom 1.0 = fitted to the viewport, aspect-correct (no scrollbars, no
//   letterbox inside the panel since its bounds keep the design aspect).
//   zoom > 1 = larger than the viewport, which then scrolls.
// =============================================================================
void JT8000ControllerEditor::layoutPanel()
{
    if (_panel == nullptr)
        return;

    const int vw = juce::jmax(1, _viewport.getWidth());
    const int vh = juce::jmax(1, _viewport.getHeight());

    // Base fit preserves the design aspect so the RML never distorts.
    const double aspect = (double) jt::JtRmlPanel::kDesignW
                        / (double) jt::JtRmlPanel::kDesignH;
    double baseW = juce::jmin((double) vw, (double) vh * aspect);
    double baseH = baseW / aspect;

    const int w = juce::roundToInt(baseW * _zoom);
    const int h = juce::roundToInt(baseH * _zoom);
    _panel->setSize(w, h);       // triggers JtRmlPanel::resized -> RML relayout

    positionEncoderStrip();      // panel-local, so they track the new size
    positionLcdEditor();
}

// Float the transparent rename editor over the LCD's top line (panel-local, so
// it scrolls/scales with the LCD). Font tracks the line height so the editor
// text matches the LCD when active.
void JT8000ControllerEditor::positionLcdEditor()
{
    if (_panel == nullptr)
        return;
    const auto r = _panel->lcdLine1Bounds();
    if (r.isEmpty())
        return;
    if (r != _lastLcdEdit)
    {
        _lastLcdEdit = r;
        _patchNameLabel.setBounds(r);
        _patchNameLabel.setFont(juce::FontOptions((float) juce::jmax(12, r.getHeight() - 8),
                                                  juce::Font::bold));
    }
}

void JT8000ControllerEditor::setZoom(double z)
{
    const double clamped = juce::jlimit(1.0, 4.0, z);
    if (juce::approximatelyEqual(clamped, _zoom))
        return;
    _zoom = clamped;
    layoutPanel();
}
// =============================================================================
// Float the encoder strip to the LEFT of the LCD. The LCD rect comes from the
// skin's laid-out element, so this tracks any window scaling or skin swap
// instead of a hard-coded position. Idempotent: it only calls setBounds when
// the target actually moved, so it is safe to poll every status tick until the
// skin has finished its first layout pass (element bounds are empty until then).
// =============================================================================
void JT8000ControllerEditor::positionEncoderStrip()
{
    if (_panel == nullptr)
        return;

    // The strip is a child of the panel, so lcdBounds() (panel-local, already
    // scaled by zoom) is exactly the coordinate space we place it in — no origin
    // offset. Size it from the LCD height so it stays proportional as the panel
    // scales/zooms rather than being a fixed 84px that shrinks next to a big LCD.
    const auto lcd = _panel->lcdBounds();
    if (lcd.isEmpty())
        return;                                     // skin not laid out yet

    const int h      = juce::jmax(48, lcd.getHeight());
    const int stripW = juce::jmax(48, juce::roundToInt(h * 1.8));   // larger nav dial
    const int gap    = juce::jmax(6,  h / 6);
    const int x      = lcd.getX() - stripW - gap;
    const int y      = lcd.getY();

    const juce::Rectangle<int> target { juce::jmax(0, x), y, stripW, h };
    if (target != _lastStripBounds)
    {
        _lastStripBounds = target;
        _encoderStrip.setBounds(target);
    }
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

    for (int i = 0; i < JtParams::kNumParams; ++i)
    {
        const auto& p = JtParams::kParams[i];

        // Keep performance/global state intact on INIT (patch reset only).
        if (!JtParams::isPatchScope(p.scope)) continue;

        if (auto* param = _proc.apvts.getParameter(p.juceId))
            param->setValueNotifyingHost(param->convertTo0to1(p.def));
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
// Patch library browser — non-modal, themed folder browser (VST3-safe)
//
// Replaces the old two-phase loadSyxFile()/showBankPicker() single-file flow.
// showLibrary() opens the browser at the last-used folder (persisted globally
// so it survives across sessions and instances — a library folder is a user
// preference, not per-project state).  The browser lists .syx/.mid files and
// their performances/patches; its callbacks feed the SAME loadPatchToLayer /
// loadPerformance functions the picker used, so load logic is unchanged.
//
// KEYBOARD: closing the browser returns keyboard focus to the editor, so the
// standalone computer-keyboard note entry (keyPressed/keyStateChanged) keeps
// working after the browser's list/buttons held focus.
// =============================================================================

juce::File JT8000ControllerEditor::lastLibraryFolder() const
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "JT8000";
    o.filenameSuffix      = "settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName          = "JT8000";
    juce::PropertiesFile props(o);

    const juce::String saved = props.getValue("libraryFolder");
    juce::File f(saved);
    if (saved.isNotEmpty() && f.isDirectory())
        return f;

    return juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
}

void JT8000ControllerEditor::setLastLibraryFolder(const juce::File& folder)
{
    if (!folder.isDirectory()) return;
    juce::PropertiesFile::Options o;
    o.applicationName     = "JT8000";
    o.filenameSuffix      = "settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName          = "JT8000";
    juce::PropertiesFile props(o);
    props.setValue("libraryFolder", folder.getFullPathName());
    props.saveIfNeeded();
}

void JT8000ControllerEditor::showLibrary()
{
    // Already open — just bring it forward.
    if (_libraryBrowser != nullptr)
    {
        /* May be hidden but not yet destroyed (see onCloseRequested). */
        _libraryBrowser->setVisible(true);
        _libraryBrowser->toFront(true);
        return;
    }

    _libraryBrowser = std::make_unique<PatchLibraryBrowser>();
    _libraryBrowser->setBounds(getLocalBounds());

    // Load a patch to a layer (A / B / Current) — unchanged load path.
    _libraryBrowser->onLoadPatch =
        [this](const JP8000SyxImporter::Patch& patch, JP8000SyxImporter::LoadTarget target)
        {
            JP8000SyxImporter::loadPatchToLayer(_proc, patch, target);
            if (_panel != nullptr)
                _panel->setPatchName(patch.name);
        };

    // Load a full performance (upper->A, lower->B, sets mode).
    _libraryBrowser->onLoadPerformance =
        [this](const JP8000SyxImporter::Performance& perf)
        {
            JP8000SyxImporter::loadPerformance(_proc, perf);
        };

    // Non-modal close.  Return keyboard focus to the editor so standalone
    // computer-keyboard note entry works again (the browser's widgets held it).
    _libraryBrowser->onCloseRequested =
        [this]
        {
            /* Runs INSIDE the browser's Close-button click. Destroying the
             * browser here would delete this std::function and the button's
             * onClick while both are still executing (use-after-free: the
             * crash on Close). So hide now, destroy next message-loop turn. */
            if (_libraryBrowser != nullptr)
                _libraryBrowser->setVisible(false);

            juce::Component::SafePointer<JT8000ControllerEditor> safe(this);
            juce::MessageManager::callAsync([safe]
            {
                if (safe == nullptr)
                    return;                         // editor closed meanwhile
                safe->_libraryBrowser.reset();
                if (safe->_isStandalone)
                    safe->grabKeyboardFocus();
            });
        };

    // Change folder — keep FileChooser lifetime here (matches existing pattern).
    _libraryBrowser->onChangeFolderRequested =
        [this] { chooseLibraryFolder(); };

    addAndMakeVisible(_libraryBrowser.get());
    _libraryBrowser->setFolder(lastLibraryFolder());
}

void JT8000ControllerEditor::chooseLibraryFolder()
{
    _fileChooser = std::make_unique<juce::FileChooser>(
        "Choose patch library folder",
        lastLibraryFolder());

    _fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc)
        {
            auto results = fc.getResults();
            if (results.isEmpty()) return;

            auto folder = results.getFirst();
            if (!folder.isDirectory()) return;

            setLastLibraryFolder(folder);
            if (_libraryBrowser != nullptr)
                _libraryBrowser->setFolder(folder);
        });
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

// =============================================================================
// Runtime skin loading
//   Points the RmlUi skin loader at a folder on disk. A jt8000.rml there (as
//   exported from the layout editor) overrides the embedded skin, and the
//   panel reloads the document immediately - a new layout with no rebuild.
//   The existing jt8000.rcss and PNGs can stay embedded; only positions, which
//   live in the RML, are being changed. Folder-select (not file-select) so the
//   loader can resolve the rml plus any sibling assets by bare name.
// =============================================================================
// =============================================================================
// Computer-keyboard note entry (standalone)
//   Maps keys to notes on the processor's shared MidiKeyboardState using JUCE's
//   standard typing layout: A S D F G H J K = white keys (C D E F G A B C),
//   W E   T Y U = black keys, Z / X = octave down / up, C / V = velocity.
//   keyStateChanged handles note-off by diffing the set of held keys.
// =============================================================================
namespace {
    // Offset in semitones from the base note for each typing key, or -1 if the
    // key is not a note. Covers the standard two-row layout.
    int semitoneForKey(int keyCode)
    {
        switch (juce::CharacterFunctions::toLowerCase((juce::juce_wchar) keyCode))
        {
            case 'a': return 0;   // C
            case 'w': return 1;   // C#
            case 's': return 2;   // D
            case 'e': return 3;   // D#
            case 'd': return 4;   // E
            case 'f': return 5;   // F
            case 't': return 6;   // F#
            case 'g': return 7;   // G
            case 'y': return 8;   // G#
            case 'h': return 9;   // A
            case 'u': return 10;  // A#
            case 'j': return 11;  // B
            case 'k': return 12;  // C (next octave)
            case 'o': return 13;
            case 'l': return 14;
            default:  return -1;
        }
    }
}

bool JT8000ControllerEditor::keyPressed(const juce::KeyPress& key)
{
    if (!_isStandalone) return false;

    const int code = key.getKeyCode();
    const juce::juce_wchar ch = juce::CharacterFunctions::toLowerCase((juce::juce_wchar) code);

    // Octave / velocity controls.
    if (ch == 'z') { _kbBaseNote = juce::jmax(0,   _kbBaseNote - 12); return true; }
    if (ch == 'x') { _kbBaseNote = juce::jmin(108, _kbBaseNote + 12); return true; }
    if (ch == 'c') { _kbVelocity = (juce::uint8) juce::jmax(1,   _kbVelocity - 10); return true; }
    if (ch == 'v') { _kbVelocity = (juce::uint8) juce::jmin(127, _kbVelocity + 10); return true; }

    const int semi = semitoneForKey(code);
    if (semi < 0) return false;

    const int note = juce::jlimit(0, 127, _kbBaseNote + semi);
    if (!_kbHeldKeys.contains(code))
    {
        _kbHeldKeys.add(code);
        _proc.getKeyboardState().noteOn(1, note, _kbVelocity / 127.0f);
    }
    return true;
}

bool JT8000ControllerEditor::keyStateChanged(bool /*isKeyDown*/)
{
    if (!_isStandalone) return false;

    // Release any held note whose key is no longer down.
    bool handled = false;
    for (int i = _kbHeldKeys.size(); --i >= 0;)
    {
        const int code = _kbHeldKeys[i];
        if (!juce::KeyPress::isKeyCurrentlyDown(code))
        {
            const int semi = semitoneForKey(code);
            if (semi >= 0)
            {
                const int note = juce::jlimit(0, 127, _kbBaseNote + semi);
                _proc.getKeyboardState().noteOff(1, note, 0.0f);
            }
            _kbHeldKeys.remove(i);
            handled = true;
        }
    }
    return handled;
}

void JT8000ControllerEditor::visibilityChanged()
{
    // Grab focus when shown so typed notes work without clicking first.
    if (_isStandalone && isVisible())
        grabKeyboardFocus();
}

void JT8000ControllerEditor::loadSkinFolder()
{
    _fileChooser = std::make_unique<juce::FileChooser>(
        "Choose a skin (.rml) file",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.rml");

    _fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode
      | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto results = fc.getResults();
            if (results.isEmpty()) return;

            const juce::File rml = results.getFirst();
            if (!rml.existsAsFile()) return;

            /* Loads by file name (any name), from the file's own folder, and
             * reloads. Sibling assets not present there fall back to embedded. */
            if (_panel != nullptr)
                _panel->loadSkinFile(rml);
            _proc.setSkinPath(rml.getFullPathName());   // persist for reopen
        });
}
