/*
 * PatchLibraryBrowser.h — Non-modal JP-8000 patch library browser
 *
 * A themed, three-zone browser that replaces the single-file SyxPickerOverlay:
 *
 *   [ Files ]        [ Contents ]                 [ Target + Load ]
 *   .syx/.mid in     Performances / Patches tabs  A / B / A+B / Current
 *   the folder       of the selected file         + Load, non-modal Close
 *
 * Flow:
 *   1. Points at a folder (last-used, persisted by the editor).  Lists every
 *      .syx/.mid child, each parsed for its performance/patch count.
 *   2. Selecting a file parses it into a ParsedBank (owned here — the bank
 *      owns its SysEx bytes, J3a, so it must outlive any load call).
 *   3. Two tabs show performances[] and orphanPatches[].  Selecting an item
 *      plus a target, then Load, fires the callbacks below.
 *
 * The component is NON-MODAL: it stays open after a Load so several patches
 * can be auditioned; Close (or onCloseRequested) dismisses it.  It performs
 * no loading itself — the editor wires the callbacks to the existing
 * loadPatchToLayer / loadPerformance functions, so load logic is unchanged.
 *
 * VST3-safe: no enterModalState (mirrors the old overlay's constraint).
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "JP8000SyxImporter.h"
#include "JtTheme.h"

class PatchLibraryBrowser : public juce::Component
{
public:
    // ---- Callbacks wired by the editor to the existing load functions ----
    /** Load one patch to a layer (A / B / Current). */
    std::function<void(const JP8000SyxImporter::Patch&,
                       JP8000SyxImporter::LoadTarget)>          onLoadPatch;
    /** Load a full performance (upper→A, lower→B, sets perf mode). */
    std::function<void(const JP8000SyxImporter::Performance&)> onLoadPerformance;
    /** User asked to close the browser (Close button). */
    std::function<void()>                                      onCloseRequested;
    /** User asked to pick a different folder — editor shows the chooser and
     *  calls setFolder() with the result (keeps FileChooser lifetime in the
     *  editor, matching the existing pattern). */
    std::function<void()>                                      onChangeFolderRequested;

    PatchLibraryBrowser();
    ~PatchLibraryBrowser() override;

    /** Point the browser at a folder and rebuild the file list. */
    void setFolder(const juce::File& folder);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // ---- Which tab of the contents pane is showing ----
    enum class Tab { Performances, Patches };

    // ---- One entry in the file list ----
    struct FileEntry
    {
        juce::File file;
        int        perfCount  = 0;
        int        patchCount = 0;
        bool       parsed     = false;   // false if the file failed to parse
    };

    // ---- Actions ----
    void rebuildFileList();
    void selectFile(int index);
    void setTab(Tab t);
    void updateContentsList();
    void updateLoadEnabled();
    void doLoad();

    bool selectedIsPerformance() const;

    // ---- Data ----
    juce::File                     _folder;
    juce::Array<FileEntry>         _files;
    JP8000SyxImporter::ParsedBank  _currentBank;   // owns bytes for loads
    int                            _selectedFile   = -1;
    int                            _selectedItem   = -1;   // row in current tab
    Tab                            _tab            = Tab::Performances;
    JP8000SyxImporter::LoadTarget  _target         = JP8000SyxImporter::LoadTarget::LayerA;
    bool                           _targetIsPerf   = false; // A+B chosen

    // ---- Sub-models (kept small and local) ----
    struct FileListModel : juce::ListBoxModel
    {
        PatchLibraryBrowser& owner;
        explicit FileListModel(PatchLibraryBrowser& o) : owner(o) {}
        int  getNumRows() override;
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
        void listBoxItemClicked(int, const juce::MouseEvent&) override;
    };
    struct ContentsModel : juce::ListBoxModel
    {
        PatchLibraryBrowser& owner;
        explicit ContentsModel(PatchLibraryBrowser& o) : owner(o) {}
        int  getNumRows() override;
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
        void listBoxItemClicked(int, const juce::MouseEvent&) override;
    };

    FileListModel _fileModel { *this };
    ContentsModel _contentsModel { *this };

    // ---- Widgets ----
    juce::Label      _title;
    juce::Label      _folderLabel;
    juce::TextButton _changeFolderBtn { "Change folder" };
    juce::TextButton _refreshBtn      { "Refresh" };
    juce::Label      _filesHead;
    juce::ListBox    _fileList        { "files",    &_fileModel };

    juce::TextButton _perfTab         { "Performances" };
    juce::TextButton _patchTab        { "Patches" };
    juce::ListBox    _contentsList    { "contents", &_contentsModel };

    juce::Label      _targetLabel;
    juce::TextButton _btnA            { "A" };
    juce::TextButton _btnB            { "B" };
    juce::TextButton _btnAB           { "A+B" };
    juce::TextButton _btnCurrent      { "Current" };
    juce::TextButton _loadBtn         { "Load" };
    juce::TextButton _closeBtn        { "Close" };

    juce::Rectangle<int> _dialogBounds;

    void styleButton(juce::TextButton&, juce::Colour textColour);
    void refreshTargetButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchLibraryBrowser)
};
