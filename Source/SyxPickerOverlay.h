/*
 * SyxPickerOverlay.h — Non-modal SYX/MID patch & performance picker
 *
 * VST3-safe overlay (no enterModalState) that shows a parsed bank's
 * contents and lets the user choose how to load them:
 *
 *   PERFORMANCES:
 *     "Perf → A+B"   — loads upper→Layer A, lower→Layer B, sets perf mode
 *     "Upper → A"    — loads just the upper patch to Layer A
 *     "Upper → B"    — loads just the upper patch to Layer B
 *     "Lower → A"    — loads just the lower patch to Layer A
 *     "Lower → B"    — loads just the lower patch to Layer B
 *
 *   ORPHAN PATCHES (no performance header):
 *     "→ A"          — loads patch to Layer A
 *     "→ B"          — loads patch to Layer B
 *     "→ Current"    — loads patch to whichever edit target is active
 *
 * RESULT CALLBACK:
 *   onResult(action, perfIndex, patchIndex)
 *     action:     PickAction enum — identifies exactly what the user chose
 *     perfIndex:  which performance was selected (-1 for orphan patches)
 *     patchIndex: which orphan patch was selected (-1 for performances)
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "JP8000SyxImporter.h"

// =========================================================================
// Pick actions — what the user wants to do with their selection
// =========================================================================
enum class PickAction : int
{
    Cancel       = 0,

    // Performance actions (perfIndex is valid)
    PerfToAB     = 1,   // load full performance: upper→A, lower→B
    UpperToA     = 2,   // load perf's upper patch to Layer A
    UpperToB     = 3,   // load perf's upper patch to Layer B
    LowerToA     = 4,   // load perf's lower patch to Layer A
    LowerToB     = 5,   // load perf's lower patch to Layer B

    // Orphan patch actions (patchIndex is valid)
    PatchToA     = 10,  // load orphan patch to Layer A
    PatchToB     = 11,  // load orphan patch to Layer B
    PatchSend    = 12,  // load + send on current edit target
    PatchOnly    = 13,  // load only (no send) on current edit target
};

// =========================================================================
// SyxPickerOverlay
// =========================================================================
class SyxPickerOverlay : public juce::Component
{
public:
    /** Result callback: action, perf index (-1 if n/a), orphan patch index (-1 if n/a). */
    std::function<void(PickAction action, int perfIndex, int patchIndex)> onResult;

    SyxPickerOverlay(const juce::String& title,
                     const JP8000SyxImporter::ParsedBank& bank)
        : _bank(bank)
    {
        const auto kBg      = juce::Colour(0xff0b0e14);
        const auto kTextHi  = juce::Colour(0xffc8d8f4);
        const auto kText    = juce::Colour(0xff8090b0);
        const auto kBorder  = juce::Colour(0xff1e2a3e);
        const auto kOrange  = juce::Colour(0xfff08020);
        const auto kGreen   = juce::Colour(0xff28ff58);
        const auto kPurple  = juce::Colour(0xff9040ff);
        const auto kBtnBg   = juce::Colour(0xff161c28);

        // Title
        _titleLabel.setText(title, juce::dontSendNotification);
        _titleLabel.setColour(juce::Label::textColourId, kTextHi);
        _titleLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        _titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(_titleLabel);

        // Styling lambdas
        auto styleCombo = [&](juce::ComboBox& cb)
        {
            cb.setColour(juce::ComboBox::backgroundColourId, kBg);
            cb.setColour(juce::ComboBox::textColourId, kTextHi);
            cb.setColour(juce::ComboBox::outlineColourId, kBorder);
        };
        auto styleBtn = [&](juce::TextButton& b, juce::Colour c)
        {
            b.setColour(juce::TextButton::buttonColourId, kBtnBg);
            b.setColour(juce::TextButton::textColourOffId, c);
        };

        // ---- Performances combo ----
        const bool hasPerfs = !bank.performances.empty();
        if (hasPerfs)
        {
            _perfLabel.setText("Performance:", juce::dontSendNotification);
            _perfLabel.setColour(juce::Label::textColourId, kText);
            _perfLabel.setFont(juce::FontOptions(11.0f));
            addAndMakeVisible(_perfLabel);

            for (int i = 0; i < static_cast<int>(bank.performances.size()); ++i)
            {
                const auto& perf = bank.performances[static_cast<size_t>(i)];
                juce::String modeTag;
                switch (perf.mode)
                {
                    case JP8000SyxImporter::PerfMode::Layer: modeTag = "[L] "; break;
                    case JP8000SyxImporter::PerfMode::Split: modeTag = "[S] "; break;
                    default:                                  modeTag = "[1] "; break;
                }

                // Build combo label: "00  [L] PerfName  (Upper / Lower)"
                // Always show patch names so the user can identify the
                // contents without selecting each entry first.
                juce::String label = juce::String(i).paddedLeft('0', 2) + "  "
                    + modeTag + perf.name;

                if (perf.hasDistinctLower)
                {
                    // Show both patch names, truncated to keep combo readable
                    juce::String upper = perf.upper.name.substring(0, 14);
                    juce::String lower = perf.lower.name.substring(0, 14);
                    label += "  (" + upper + " / " + lower + ")";
                }
                else if (perf.upper.name.isNotEmpty()
                         && perf.upper.name != perf.name)
                {
                    // Single patch — show its name if different from the
                    // performance name (avoids redundant display)
                    label += "  (" + perf.upper.name.substring(0, 18) + ")";
                }

                _perfCombo.addItem(label, i + 1);
            }
            _perfCombo.setSelectedItemIndex(0, juce::dontSendNotification);
            styleCombo(_perfCombo);
            addAndMakeVisible(_perfCombo);

            // Update detail label when selection changes
            _perfCombo.onChange = [this] { updatePerfDetail(); };

            // Detail label (shows upper/lower names for selected perf)
            _perfDetailLabel.setColour(juce::Label::textColourId, kText);
            _perfDetailLabel.setFont(juce::FontOptions(10.0f));
            addAndMakeVisible(_perfDetailLabel);

            // Performance buttons
            styleBtn(_perfToABBtn,  kOrange);
            styleBtn(_upperToABtn,  kGreen);
            styleBtn(_upperToBBtn,  kGreen);
            styleBtn(_lowerToABtn,  kPurple);
            styleBtn(_lowerToBBtn,  kPurple);

            _perfToABBtn.onClick  = [this] { finish(PickAction::PerfToAB, perfIdx(), -1); };
            _upperToABtn.onClick  = [this] { finish(PickAction::UpperToA, perfIdx(), -1); };
            _upperToBBtn.onClick  = [this] { finish(PickAction::UpperToB, perfIdx(), -1); };
            _lowerToABtn.onClick  = [this] { finish(PickAction::LowerToA, perfIdx(), -1); };
            _lowerToBBtn.onClick  = [this] { finish(PickAction::LowerToB, perfIdx(), -1); };

            addAndMakeVisible(_perfToABBtn);
            addAndMakeVisible(_upperToABtn);
            addAndMakeVisible(_upperToBBtn);
            addAndMakeVisible(_lowerToABtn);
            addAndMakeVisible(_lowerToBBtn);

            updatePerfDetail();
        }

        // ---- Orphan patches combo ----
        const bool hasOrphans = !bank.orphanPatches.empty();
        if (hasOrphans)
        {
            _patchLabel.setText("Patches:", juce::dontSendNotification);
            _patchLabel.setColour(juce::Label::textColourId, kText);
            _patchLabel.setFont(juce::FontOptions(11.0f));
            addAndMakeVisible(_patchLabel);

            for (int i = 0; i < static_cast<int>(bank.orphanPatches.size()); ++i)
            {
                _patchCombo.addItem(
                    juce::String(i).paddedLeft('0', 2) + "  "
                    + bank.orphanPatches[static_cast<size_t>(i)].name,
                    i + 1);
            }
            _patchCombo.setSelectedItemIndex(0, juce::dontSendNotification);
            styleCombo(_patchCombo);
            addAndMakeVisible(_patchCombo);

            styleBtn(_patchToABtn,   kGreen);
            styleBtn(_patchToBBtn,   kPurple);
            styleBtn(_patchSendBtn,  kOrange);
            styleBtn(_patchOnlyBtn,  kText);

            _patchToABtn.onClick   = [this] { finish(PickAction::PatchToA,  -1, patchIdx()); };
            _patchToBBtn.onClick   = [this] { finish(PickAction::PatchToB,  -1, patchIdx()); };
            _patchSendBtn.onClick  = [this] { finish(PickAction::PatchSend, -1, patchIdx()); };
            _patchOnlyBtn.onClick  = [this] { finish(PickAction::PatchOnly, -1, patchIdx()); };

            addAndMakeVisible(_patchToABtn);
            addAndMakeVisible(_patchToBBtn);
            addAndMakeVisible(_patchSendBtn);
            addAndMakeVisible(_patchOnlyBtn);
        }

        // Cancel button — always present
        styleBtn(_cancelBtn, juce::Colour(0xff8090b0));
        _cancelBtn.onClick = [this] { finish(PickAction::Cancel, -1, -1); };
        addAndMakeVisible(_cancelBtn);

        _hasPerfs   = hasPerfs;
        _hasOrphans = hasOrphans;

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
        // Calculate dialog height based on content
        int contentH = 32;  // title + padding
        if (_hasPerfs)   contentH += 110;  // label + combo + detail + buttons
        if (_hasOrphans) contentH += 80;   // label + combo + buttons
        contentH += 40;                    // cancel button + padding

        const int dlgW = juce::jmin(520, getWidth() - 40);
        const int dlgH = juce::jmin(contentH, getHeight() - 40);
        _dialogBounds = getLocalBounds().withSizeKeepingCentre(dlgW, dlgH);

        auto area = _dialogBounds.reduced(16);
        const int pad = 6;

        _titleLabel.setBounds(area.removeFromTop(20));
        area.removeFromTop(pad);

        if (_hasPerfs)
        {
            _perfLabel.setBounds(area.removeFromTop(16));
            area.removeFromTop(2);
            _perfCombo.setBounds(area.removeFromTop(26));
            area.removeFromTop(2);
            _perfDetailLabel.setBounds(area.removeFromTop(14));
            area.removeFromTop(4);

            // Performance button row
            auto perfRow = area.removeFromTop(28);
            const int nPerfBtns = 5;
            const int perfBtnW = (perfRow.getWidth() - (nPerfBtns - 1) * 4) / nPerfBtns;
            _perfToABBtn.setBounds(perfRow.removeFromLeft(perfBtnW));
            perfRow.removeFromLeft(4);
            _upperToABtn.setBounds(perfRow.removeFromLeft(perfBtnW));
            perfRow.removeFromLeft(4);
            _upperToBBtn.setBounds(perfRow.removeFromLeft(perfBtnW));
            perfRow.removeFromLeft(4);
            _lowerToABtn.setBounds(perfRow.removeFromLeft(perfBtnW));
            perfRow.removeFromLeft(4);
            _lowerToBBtn.setBounds(perfRow);

            area.removeFromTop(pad);
        }

        if (_hasOrphans)
        {
            _patchLabel.setBounds(area.removeFromTop(16));
            area.removeFromTop(2);
            _patchCombo.setBounds(area.removeFromTop(26));
            area.removeFromTop(4);

            auto patchRow = area.removeFromTop(28);
            const int nPatchBtns = 4;
            const int patchBtnW = (patchRow.getWidth() - (nPatchBtns - 1) * 4) / nPatchBtns;
            _patchToABtn.setBounds(patchRow.removeFromLeft(patchBtnW));
            patchRow.removeFromLeft(4);
            _patchToBBtn.setBounds(patchRow.removeFromLeft(patchBtnW));
            patchRow.removeFromLeft(4);
            _patchSendBtn.setBounds(patchRow.removeFromLeft(patchBtnW));
            patchRow.removeFromLeft(4);
            _patchOnlyBtn.setBounds(patchRow);

            area.removeFromTop(pad);
        }

        _cancelBtn.setBounds(area.removeFromTop(28));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!_dialogBounds.contains(e.getPosition()))
            finish(PickAction::Cancel, -1, -1);
    }

private:
    int perfIdx()  const { return _perfCombo.getSelectedItemIndex(); }
    int patchIdx() const { return _patchCombo.getSelectedItemIndex(); }

    void updatePerfDetail()
    {
        int idx = _perfCombo.getSelectedItemIndex();
        if (idx >= 0 && idx < static_cast<int>(_bank.performances.size()))
        {
            const auto& perf = _bank.performances[static_cast<size_t>(idx)];

            // Always show the upper patch name. When layers differ, show
            // both on the same line separated by a divider.
            juce::String detail;
            if (perf.hasDistinctLower)
                detail = "Upper: " + perf.upper.name + "   |   Lower: " + perf.lower.name;
            else
                detail = "Patch: " + perf.upper.name;

            _perfDetailLabel.setText(detail, juce::dontSendNotification);
        }
    }

    void finish(PickAction action, int perfIndex, int patchIndex)
    {
        if (onResult)
            onResult(action, perfIndex, patchIndex);
        if (auto* parent = getParentComponent())
            parent->removeChildComponent(this);
    }

    const JP8000SyxImporter::ParsedBank& _bank;
    bool _hasPerfs   = false;
    bool _hasOrphans = false;

    juce::Label      _titleLabel;

    // Performance section
    juce::Label      _perfLabel;
    juce::ComboBox   _perfCombo;
    juce::Label      _perfDetailLabel;
    juce::TextButton _perfToABBtn  { "Perf > A+B" };
    juce::TextButton _upperToABtn  { "Upper > A" };
    juce::TextButton _upperToBBtn  { "Upper > B" };
    juce::TextButton _lowerToABtn  { "Lower > A" };
    juce::TextButton _lowerToBBtn  { "Lower > B" };

    // Orphan patch section
    juce::Label      _patchLabel;
    juce::ComboBox   _patchCombo;
    juce::TextButton _patchToABtn  { "> A" };
    juce::TextButton _patchToBBtn  { "> B" };
    juce::TextButton _patchSendBtn { "Load + Send" };
    juce::TextButton _patchOnlyBtn { "Load Only" };

    // Always visible
    juce::TextButton _cancelBtn    { "Cancel" };

    juce::Rectangle<int> _dialogBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyxPickerOverlay)
};
