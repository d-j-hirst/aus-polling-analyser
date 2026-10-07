#pragma once

#include "LiveSnapshotSequence.h"
#include <wx/wx.h>
#include <wx/spinctrl.h>

// Edits a saved selection. Opening the dialog immediately scans either the
// remembered folder or the standard election folder; no Browse step is needed.
class LiveSnapshotSequenceDialog : public wxDialog {
public:
    LiveSnapshotSequenceDialog(wxWindow* parent, std::filesystem::path root,
        LiveSnapshotSequence::Settings settings);
    LiveSnapshotSequence::Settings selection() const;
private:
    void scan(bool notifyError = false);
    void preview();
    void addSelected();
    void addNext();
    std::filesystem::path root;
    LiveSnapshotSequence::Settings settings;
    std::vector<LiveResultsInput::CurrentFile> available;
    wxTextCtrl* folderInput = nullptr;
    wxListBox* availableList = nullptr;
    wxTextCtrl* sequenceInput = nullptr;
    wxSpinCtrl* nextCount = nullptr;
    wxTextCtrl* previewText = nullptr;
    wxStaticText* status = nullptr;
    wxButton* saveButton = nullptr;
    std::string scanError;
};
