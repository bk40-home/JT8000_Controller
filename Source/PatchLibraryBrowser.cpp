/*
 * PatchLibraryBrowser.cpp — see PatchLibraryBrowser.h
 */

#include "PatchLibraryBrowser.h"

using namespace JP8000SyxImporter;

// =============================================================================
// Construction / styling
// =============================================================================
PatchLibraryBrowser::PatchLibraryBrowser()
{
    // NB: deliberately NOT setAlwaysOnTop — an always-on-top child becomes its
    // own keyboard-focus root, which stops the JUCE standalone wrapper's
    // computer-keyboard (QWERTY→MIDI) feature receiving keys, and can leave it
    // dead after the browser closes.  Being added last already draws it over
    // the panel; that is enough for a modal-style overlay.

    _title.setText("Patch Library", juce::dontSendNotification);
    _title.setColour(juce::Label::textColourId, JtTheme::textHi);
    _title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(_title);

    _folderLabel.setColour(juce::Label::textColourId, JtTheme::textDim);
    _folderLabel.setFont(juce::FontOptions(11.0f));
    _folderLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(_folderLabel);

    _filesHead.setText("FILES", juce::dontSendNotification);
    _filesHead.setColour(juce::Label::textColourId, JtTheme::textDim);
    _filesHead.setFont(juce::FontOptions(10.0f));
    addAndMakeVisible(_filesHead);

    styleButton(_changeFolderBtn, JtTheme::text);
    styleButton(_refreshBtn,      JtTheme::text);
    _changeFolderBtn.onClick = [this] { if (onChangeFolderRequested) onChangeFolderRequested(); };
    _refreshBtn.onClick      = [this] { rebuildFileList(); };
    addAndMakeVisible(_changeFolderBtn);
    addAndMakeVisible(_refreshBtn);

    _fileList.setColour(juce::ListBox::backgroundColourId, JtTheme::panel2);
    _fileList.setRowHeight(26);
    addAndMakeVisible(_fileList);

    styleButton(_perfTab,  JtTheme::textHi);
    styleButton(_patchTab, JtTheme::text);
    _perfTab.onClick  = [this] { setTab(Tab::Performances); };
    _patchTab.onClick = [this] { setTab(Tab::Patches); };
    addAndMakeVisible(_perfTab);
    addAndMakeVisible(_patchTab);

    _contentsList.setColour(juce::ListBox::backgroundColourId, JtTheme::panel2);
    _contentsList.setRowHeight(28);
    addAndMakeVisible(_contentsList);

    _targetLabel.setText("Load to:", juce::dontSendNotification);
    _targetLabel.setColour(juce::Label::textColourId, JtTheme::text);
    _targetLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(_targetLabel);

    _btnA.onClick       = [this] { _target = LoadTarget::LayerA; _targetIsPerf = false; refreshTargetButtons(); };
    _btnB.onClick       = [this] { _target = LoadTarget::LayerB; _targetIsPerf = false; refreshTargetButtons(); };
    _btnAB.onClick      = [this] { _targetIsPerf = true;                                refreshTargetButtons(); };
    _btnCurrent.onClick = [this] { _target = LoadTarget::Current; _targetIsPerf = false; refreshTargetButtons(); };
    for (auto* b : { &_btnA, &_btnB, &_btnAB, &_btnCurrent })
    {
        styleButton(*b, JtTheme::text);
        addAndMakeVisible(*b);
    }

    styleButton(_loadBtn,  JtTheme::onAccent);
    _loadBtn.setColour(juce::TextButton::buttonColourId, JtTheme::accent);
    _loadBtn.onClick = [this] { doLoad(); };
    addAndMakeVisible(_loadBtn);

    styleButton(_closeBtn, JtTheme::text);
    _closeBtn.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
    addAndMakeVisible(_closeBtn);

    refreshTargetButtons();
    updateLoadEnabled();
}

PatchLibraryBrowser::~PatchLibraryBrowser() = default;

void PatchLibraryBrowser::styleButton(juce::TextButton& b, juce::Colour textColour)
{
    b.setColour(juce::TextButton::buttonColourId,   JtTheme::row);
    b.setColour(juce::TextButton::textColourOffId,  textColour);
    b.setColour(juce::TextButton::textColourOnId,   JtTheme::textHi);
}

// =============================================================================
// Folder handling
// =============================================================================
void PatchLibraryBrowser::setFolder(const juce::File& folder)
{
    _folder = folder;
    _folderLabel.setText(folder.getFullPathName(), juce::dontSendNotification);
    rebuildFileList();
}

void PatchLibraryBrowser::rebuildFileList()
{
    _files.clear();
    _selectedFile = -1;
    _selectedItem = -1;
    _currentBank = ParsedBank{};   // drop the previous bank's bytes

    if (_folder.isDirectory())
    {
        auto found = _folder.findChildFiles(
            juce::File::findFiles, /*recursive*/ false,
            "*.syx;*.mid;*.SYX;*.MID");
        found.sort();

        for (const auto& f : found)
        {
            FileEntry e;
            e.file = f;

            // Parse only for counts — cheap enough for a folder of banks, and
            // it lets the list show contents without a second pass on select.
            juce::MemoryBlock mb;
            if (f.loadFileAsData(mb) && mb.getSize() > 0)
            {
                auto bank = parseFile(static_cast<const uint8_t*>(mb.getData()),
                                      mb.getSize(),
                                      f.getFileExtension().toLowerCase() == ".mid");
                e.perfCount  = static_cast<int>(bank.performances.size());
                e.patchCount = static_cast<int>(bank.orphanPatches.size());
                e.parsed     = (e.perfCount + e.patchCount) > 0;
            }
            _files.add(e);
        }
    }

    _fileList.updateContent();
    _contentsList.updateContent();
    updateLoadEnabled();
    repaint();
}

// =============================================================================
// Selection
// =============================================================================
void PatchLibraryBrowser::selectFile(int index)
{
    if (index < 0 || index >= _files.size()) return;
    _selectedFile = index;
    _selectedItem = -1;

    // Re-parse the chosen file into the owned bank (its bytes back the loads).
    _currentBank = ParsedBank{};
    const auto& entry = _files.getReference(index);
    juce::MemoryBlock mb;
    if (entry.file.loadFileAsData(mb) && mb.getSize() > 0)
    {
        _currentBank = parseFile(static_cast<const uint8_t*>(mb.getData()),
                                 mb.getSize(),
                                 entry.file.getFileExtension().toLowerCase() == ".mid");
    }

    // Default the tab to whichever list has content.
    if (_currentBank.performances.empty() && !_currentBank.orphanPatches.empty())
        _tab = Tab::Patches;
    else
        _tab = Tab::Performances;

    setTab(_tab);
    _fileList.repaint();
    updateLoadEnabled();
}

void PatchLibraryBrowser::setTab(Tab t)
{
    _tab = t;
    _selectedItem = -1;

    const bool perf = (t == Tab::Performances);
    _perfTab.setColour(juce::TextButton::textColourOffId,  perf ? JtTheme::accent : JtTheme::text);
    _patchTab.setColour(juce::TextButton::textColourOffId, perf ? JtTheme::text   : JtTheme::accent);

    updateContentsList();
}

void PatchLibraryBrowser::updateContentsList()
{
    _contentsList.updateContent();
    _contentsList.deselectAllRows();
    _contentsList.repaint();
    updateLoadEnabled();
}

bool PatchLibraryBrowser::selectedIsPerformance() const
{
    return _tab == Tab::Performances && _selectedItem >= 0
        && _selectedItem < static_cast<int>(_currentBank.performances.size());
}

// =============================================================================
// Load-target buttons
// =============================================================================
void PatchLibraryBrowser::refreshTargetButtons()
{
    auto mark = [this](juce::TextButton& b, bool on)
    {
        b.setColour(juce::TextButton::buttonColourId,
                    on ? JtTheme::accent : JtTheme::row);
        b.setColour(juce::TextButton::textColourOffId,
                    on ? JtTheme::onAccent : JtTheme::text);
    };
    mark(_btnA,       !_targetIsPerf && _target == LoadTarget::LayerA);
    mark(_btnB,       !_targetIsPerf && _target == LoadTarget::LayerB);
    mark(_btnAB,       _targetIsPerf);
    mark(_btnCurrent, !_targetIsPerf && _target == LoadTarget::Current);
    updateLoadEnabled();
}

void PatchLibraryBrowser::updateLoadEnabled()
{
    const bool haveSel = _selectedItem >= 0;

    // A+B only makes sense for a performance; disable it for orphan patches.
    _btnAB.setEnabled(selectedIsPerformance());

    // If A+B is selected but the current selection is not a performance, fall
    // back to Layer A so Load stays meaningful.
    if (_targetIsPerf && !selectedIsPerformance())
    {
        _targetIsPerf = false;
        _target = LoadTarget::LayerA;
        refreshTargetButtons();
        return;   // refreshTargetButtons() re-enters updateLoadEnabled()
    }

    _loadBtn.setEnabled(haveSel);
}

// =============================================================================
// Load
// =============================================================================
void PatchLibraryBrowser::doLoad()
{
    if (_selectedItem < 0) return;

    if (_tab == Tab::Performances)
    {
        if (_selectedItem >= static_cast<int>(_currentBank.performances.size())) return;
        const auto& perf = _currentBank.performances[static_cast<size_t>(_selectedItem)];

        if (_targetIsPerf)
        {
            if (onLoadPerformance) onLoadPerformance(perf);
        }
        else
        {
            // Single-layer load of a performance uses its UPPER patch (matches
            // the old Upper→A / Upper→B behaviour).
            if (onLoadPatch) onLoadPatch(perf.upper, _target);
        }
    }
    else
    {
        if (_selectedItem >= static_cast<int>(_currentBank.orphanPatches.size())) return;
        const auto& patch = _currentBank.orphanPatches[static_cast<size_t>(_selectedItem)];
        if (onLoadPatch) onLoadPatch(patch, _target);
    }
    // Non-modal: stay open after loading.
}

// =============================================================================
// Painting — themed dialog panel
// =============================================================================
void PatchLibraryBrowser::paint(juce::Graphics& g)
{
    g.fillAll(JtTheme::scrim);
    g.setColour(JtTheme::panel2);
    g.fillRoundedRectangle(_dialogBounds.toFloat(), 10.0f);
    g.setColour(JtTheme::border);
    g.drawRoundedRectangle(_dialogBounds.toFloat(), 10.0f, 1.5f);

    // Divider between file list and contents.
    const int splitX = _dialogBounds.getX() + 240;
    g.setColour(JtTheme::borderSoft);
    g.drawVerticalLine(splitX,
                       (float) (_dialogBounds.getY() + 44),
                       (float) (_dialogBounds.getBottom() - 60));
}

// =============================================================================
// Layout
// =============================================================================
void PatchLibraryBrowser::resized()
{
    const int W = 760, H = 480;
    _dialogBounds = juce::Rectangle<int>(0, 0, W, H).withCentre(getLocalBounds().getCentre());
    auto r = _dialogBounds.reduced(0);

    // Title bar
    auto titleBar = r.removeFromTop(44).reduced(16, 8);
    _title.setBounds(titleBar.removeFromLeft(180));
    _refreshBtn.setBounds(titleBar.removeFromRight(72).reduced(2));
    _changeFolderBtn.setBounds(titleBar.removeFromRight(110).reduced(2));
    _folderLabel.setBounds(titleBar);

    // Footer (target + load + close)
    auto footer = r.removeFromBottom(56).reduced(16, 10);
    _targetLabel.setBounds(footer.removeFromLeft(56));
    auto segArea = footer.removeFromLeft(300);
    const int segW = segArea.getWidth() / 4;
    _btnA.setBounds       (segArea.removeFromLeft(segW).reduced(1));
    _btnB.setBounds       (segArea.removeFromLeft(segW).reduced(1));
    _btnAB.setBounds      (segArea.removeFromLeft(segW).reduced(1));
    _btnCurrent.setBounds (segArea.reduced(1));
    _closeBtn.setBounds(footer.removeFromRight(90).reduced(2));
    _loadBtn.setBounds(footer.removeFromRight(110).reduced(2));

    // Body: files (left) | contents (right)
    auto body = r.reduced(0, 0);
    auto left = body.removeFromLeft(240);
    _filesHead.setBounds(left.removeFromTop(24).reduced(12, 4));
    _fileList.setBounds(left.reduced(8, 4));

    auto right = body;
    auto tabs = right.removeFromTop(30).reduced(12, 4);
    _perfTab.setBounds (tabs.removeFromLeft(130));
    _patchTab.setBounds(tabs.removeFromLeft(100));
    _contentsList.setBounds(right.reduced(8, 4));
}

// =============================================================================
// File list model
// =============================================================================
int PatchLibraryBrowser::FileListModel::getNumRows()
{
    return owner._files.size();
}

void PatchLibraryBrowser::FileListModel::paintListBoxItem(
    int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= owner._files.size()) return;
    const auto& e = owner._files.getReference(row);

    if (selected)
    {
        g.setColour(JtTheme::rowSelected);
        g.fillRect(0, 0, width, height);
        g.setColour(JtTheme::accent);
        g.fillRect(0, 0, 2, height);   // orange left marker
    }

    g.setColour(selected ? JtTheme::textHi : JtTheme::text);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(e.file.getFileName(),
               10, 0, width - 44, height, juce::Justification::centredLeft, true);

    // Count on the right
    const int total = e.perfCount + e.patchCount;
    g.setColour(JtTheme::textDim);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(juce::String(total),
               width - 34, 0, 28, height, juce::Justification::centredRight, false);
}

void PatchLibraryBrowser::FileListModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    owner.selectFile(row);
}

// =============================================================================
// Contents list model
// =============================================================================
int PatchLibraryBrowser::ContentsModel::getNumRows()
{
    if (owner._tab == Tab::Performances)
        return static_cast<int>(owner._currentBank.performances.size());
    return static_cast<int>(owner._currentBank.orphanPatches.size());
}

void PatchLibraryBrowser::ContentsModel::paintListBoxItem(
    int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected)
    {
        g.setColour(JtTheme::rowSelected);
        g.fillRoundedRectangle(2.0f, 2.0f, (float) width - 4, (float) height - 4, 5.0f);
    }

    g.setFont(juce::FontOptions(12.0f));

    // Row number
    g.setColour(JtTheme::textDim);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(juce::String(row).paddedLeft('0', 2),
               10, 0, 24, height, juce::Justification::centredLeft, false);

    if (owner._tab == Tab::Performances)
    {
        if (row < 0 || row >= (int) owner._currentBank.performances.size()) return;
        const auto& perf = owner._currentBank.performances[(size_t) row];

        // Mode tag
        juce::String tag; juce::Colour tagBg, tagFg;
        switch (perf.mode)
        {
            case PerfMode::Layer: tag = "LYR"; tagBg = JtTheme::accentDim; tagFg = JtTheme::accent; break;
            case PerfMode::Split: tag = "SPL"; tagBg = juce::Colour(0xff14263a); tagFg = juce::Colour(0xff5aa0e0); break;
            default:              tag = "SNG"; tagBg = JtTheme::row;        tagFg = JtTheme::text;   break;
        }
        g.setColour(tagBg);
        g.fillRoundedRectangle(38.0f, (float) height * 0.5f - 8.0f, 32.0f, 16.0f, 3.0f);
        g.setColour(tagFg);
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(tag, 38, 0, 32, height, juce::Justification::centred, false);

        // Name
        g.setColour(selected ? JtTheme::textHi : JtTheme::textHi);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(perf.name, 78, 0, width - 78 - 210, height,
                   juce::Justification::centredLeft, true);

        // Upper / lower sub-text
        juce::String sub = perf.hasDistinctLower
            ? (perf.upper.name.substring(0, 14) + " / " + perf.lower.name.substring(0, 14))
            : perf.upper.name;
        g.setColour(JtTheme::textDim);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(sub, width - 210, 0, 200, height,
                   juce::Justification::centredRight, true);
    }
    else
    {
        if (row < 0 || row >= (int) owner._currentBank.orphanPatches.size()) return;
        const auto& patch = owner._currentBank.orphanPatches[(size_t) row];
        g.setColour(JtTheme::textHi);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(patch.name, 40, 0, width - 50, height,
                   juce::Justification::centredLeft, true);
    }
}

void PatchLibraryBrowser::ContentsModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    owner._selectedItem = row;
    owner.updateLoadEnabled();
}
