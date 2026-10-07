#include "LiveSnapshotSequenceDialog.h"

#include "LiveSnapshotData.h"
#include <wx/dirdlg.h>
#include <wx/spinctrl.h>

LiveSnapshotSequenceDialog::LiveSnapshotSequenceDialog(wxWindow* parent,
    std::filesystem::path root, LiveSnapshotSequence::Settings settings)
    : wxDialog(parent, wxID_ANY, "Configure snapshot sequence", wxDefaultPosition,
        wxSize(920, 660), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    root(std::move(root)), settings(std::move(settings)) {
    auto outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(
        "Select the saved feeds to run for " + this->settings.election +
        ". Results are retained for the Live Booths tab.")), 0, wxALL, 10);
    auto folderRow = new wxBoxSizer(wxHORIZONTAL);
    folderRow->Add(new wxStaticText(this, wxID_ANY, "Snapshot folder:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    folderInput = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(LiveResultsInput::pathToUtf8(
        LiveSnapshotSequence::directory(this->root, this->settings))));
    auto browse = new wxButton(this, wxID_ANY, "Browse...");
    folderRow->Add(folderInput, 1, wxRIGHT, 8);
    folderRow->Add(browse);
    outer->Add(folderRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    auto columns = new wxBoxSizer(wxHORIZONTAL);
    auto left = new wxBoxSizer(wxVERTICAL);
    left->Add(new wxStaticText(this, wxID_ANY, "Available snapshots (chronological):"), 0, wxBOTTOM, 5);
    availableList = new wxListBox(this, wxID_ANY);
    left->Add(availableList, 1, wxEXPAND);
    auto add = new wxButton(this, wxID_ANY, "Add selected code");
    left->Add(add, 0, wxTOP, 5);
    auto right = new wxBoxSizer(wxVERTICAL);
    right->Add(new wxStaticText(this, wxID_ANY, "Sequence: one timestamp code per line"), 0, wxBOTTOM, 5);
    sequenceInput = new wxTextCtrl(this, wxID_ANY,
        wxString::FromUTF8(LiveSnapshotSequence::codeText(this->settings.codes)),
        wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    right->Add(sequenceInput, 1, wxEXPAND);
    auto nextRow = new wxBoxSizer(wxHORIZONTAL);
    auto next = new wxButton(this, wxID_ANY, "Add next");
    nextCount = new wxSpinCtrl(this, wxID_ANY, "5", wxDefaultPosition, wxSize(75, -1), wxSP_ARROW_KEYS, 1, 100000, 5);
    nextRow->Add(next, 0, wxRIGHT, 5);
    nextRow->Add(nextCount, 0, wxRIGHT, 5);
    nextRow->Add(new wxStaticText(this, wxID_ANY, "after the last entered code"), 0, wxALIGN_CENTER_VERTICAL);
    right->Add(nextRow, 0, wxTOP, 5);
    columns->Add(left, 1, wxRIGHT | wxEXPAND, 10);
    columns->Add(right, 1, wxEXPAND);
    outer->Add(columns, 1, wxEXPAND | wxALL, 10);
    outer->Add(new wxStaticText(this, wxID_ANY, "Resolved sequence:"), 0, wxLEFT | wxRIGHT, 10);
    previewText = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 100), wxTE_MULTILINE | wxTE_READONLY);
    outer->Add(previewText, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    status = new wxStaticText(this, wxID_ANY, "");
    outer->Add(status, 0, wxEXPAND | wxALL, 10);
    auto buttons = new wxStdDialogButtonSizer;
    saveButton = new wxButton(this, wxID_OK, "Save sequence");
    buttons->AddButton(saveButton);
    buttons->AddButton(new wxButton(this, wxID_CANCEL));
    buttons->Realize();
    outer->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
    SetSizer(outer);
    SetMinSize(wxSize(850, 600));
    CentreOnParent();

    folderInput->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { scan(); });
    sequenceInput->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { preview(); });
    add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { addSelected(); });
    availableList->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { addSelected(); });
    next->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { addNext(); });
    browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxDirDialog dialog(this, "Select snapshot folder", folderInput->GetValue());
        if (dialog.ShowModal() == wxID_OK) folderInput->SetValue(dialog.GetPath());
    });
    // Populate the default/remembered folder as soon as the dialog opens.
    scan(true);
}

LiveSnapshotSequence::Settings LiveSnapshotSequenceDialog::selection() const {
    auto selected = settings;
    // Keep a custom folder portable in the project file. Convert the native
    // control text explicitly to UTF-8 so non-ASCII Windows paths survive save.
    auto path = LiveResultsInput::pathFromUtf8(folderInput->GetValue().ToStdString(wxConvUTF8));
    auto defaultSettings = settings;
    defaultSettings.folder.clear();
    selected.folder = path.lexically_normal() == LiveSnapshotSequence::directory(root, defaultSettings).lexically_normal()
        ? "" : LiveSnapshotSequence::portableFolder(root, path);
    selected.codes = LiveSnapshotSequence::parseCodes(sequenceInput->GetValue().ToStdString());
    return selected;
}

void LiveSnapshotSequenceDialog::scan(bool notifyError) {
    // Discover filenames only: opening or editing this dialog must not ingest
    // future votes into the election's accumulated counting history.
    available.clear();
    availableList->Clear();
    scanError.clear();
    try {
        auto selected = settings;
        selected.folder = folderInput->GetValue().ToStdString(wxConvUTF8);
        auto discovery = LiveSnapshotSequence::discover(LiveSnapshotSequence::directory(root, selected), settings.election);
        available = std::move(discovery.files);
        scanError = std::move(discovery.error);
        for (auto const& file : available)
            availableList->Append(wxString::FromUTF8(*file.timestamp + "  |  " +
                LiveSnapshot::formatSnapshotTimestamp(*file.timestamp) + "  |  " + LiveResultsInput::pathToUtf8(file.path.filename())));
    }
    catch (std::exception const& error) { scanError = error.what(); }
    preview();
    // Explain an unavailable initial folder once, leaving the editor open so
    // the user can choose another folder. Typing a path uses inline feedback
    // instead of opening a message box for every incomplete keystroke.
    if (notifyError && !scanError.empty())
        wxMessageBox(wxString::FromUTF8(scanError), "Cannot read snapshot folder", wxOK | wxICON_ERROR, this);
}

void LiveSnapshotSequenceDialog::preview() {
    // Resolve entered codes against the current collection before accepting
    // the settings, and show the files that an unattended run would consume.
    if (!scanError.empty()) {
        previewText->ChangeValue("");
        status->SetLabel(wxString::FromUTF8(scanError));
        saveButton->Disable();
        return;
    }
    try {
        auto codes = LiveSnapshotSequence::parseCodes(sequenceInput->GetValue().ToStdString());
        auto selected = LiveSnapshotSequence::resolve(available, codes);
        std::string text;
        for (auto const& file : selected)
            text += *file.timestamp + "  " + LiveResultsInput::pathToUtf8(file.path.filename()) + "\n";
        previewText->ChangeValue(wxString::FromUTF8(text));
        status->SetLabel(wxString::FromUTF8(std::to_string(available.size()) + " available; " +
            std::to_string(selected.size()) + " selected. Existing simulation output folders will be used; empty ones get a default."));
        saveButton->Enable();
    }
    catch (std::exception const& error) {
        previewText->ChangeValue("");
        status->SetLabel(wxString::FromUTF8(error.what()));
        saveButton->Disable();
    }
}

void LiveSnapshotSequenceDialog::addSelected() {
    int index = availableList->GetSelection();
    if (index == wxNOT_FOUND) return;
    auto text = sequenceInput->GetValue();
    if (!text.empty() && !text.EndsWith("\n")) text += "\n";
    sequenceInput->SetValue(text + wxString::FromUTF8(*available.at(std::size_t(index)).timestamp));
}

void LiveSnapshotSequenceDialog::addNext() {
    // Expand the shortcut now, rather than at replay time, so newly received
    // files cannot silently extend or alter an already saved sequence.
    try {
        auto codes = LiveSnapshotSequence::parseCodes(sequenceInput->GetValue().ToStdString());
        LiveSnapshotSequence::appendNext(available, codes, std::size_t(nextCount->GetValue()));
        sequenceInput->SetValue(wxString::FromUTF8(LiveSnapshotSequence::codeText(codes)));
    }
    catch (std::exception const& error) { status->SetLabel(wxString::FromUTF8(error.what())); }
}
