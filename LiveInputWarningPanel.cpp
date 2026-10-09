#include "LiveInputWarningPanel.h"
#include <wx/sizer.h>
#include <sstream>

namespace {
std::string countsText(nlohmann::json const& values) {
    if (values.is_null()) return "unavailable";
    std::ostringstream text;
    long long total = 0;
    for (auto const& pair : values) {
        int candidate = pair.at(0), count = pair.at(1);
        text << "candidate " << candidate << ": " << count << "; "; total += count;
    }
    text << "total " << total;
    return text.str();
}
}

LiveInputWarningPanel::LiveInputWarningPanel(wxWindow* parent) : wxPanel(parent) {
    auto sizer = new wxBoxSizer(wxVERTICAL);
    heading = new wxStaticText(this,wxID_ANY,"");
    routine = new wxCheckBox(this,wxID_ANY,"Show routine incomplete-count information");
    rows = new wxListCtrl(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxLC_REPORT | wxLC_SINGLE_SEL);
    rows->InsertColumn(0,"Issue",wxLIST_FORMAT_LEFT,190);
    rows->InsertColumn(1,"Records",wxLIST_FORMAT_LEFT,65);
    rows->InsertColumn(2,"Districts",wxLIST_FORMAT_LEFT,65);
    rows->InsertColumn(3,"Most severe example",wxLIST_FORMAT_LEFT,260);
    rows->InsertColumn(4,"Recovery / operator action",wxLIST_FORMAT_LEFT,600);
    sizer->Add(heading,0,wxEXPAND | wxALL,4);
    sizer->Add(routine,0,wxLEFT | wxRIGHT | wxBOTTOM,4);
    sizer->Add(rows,1,wxEXPAND | wxALL,4);
    SetSizer(sizer);
    routine->Bind(wxEVT_CHECKBOX,[this](wxCommandEvent&) { rebuildRows(); });
    rows->Bind(wxEVT_LIST_ITEM_SELECTED,&LiveInputWarningPanel::showDetails,this);
    Hide();
}

void LiveInputWarningPanel::setDiagnostic(nlohmann::json value, nlohmann::json affected) {
    diagnostic = std::move(value); details = std::move(affected);
    rebuildRows();
}

void LiveInputWarningPanel::rebuildRows() {
    rows->DeleteAllItems(); displayedTypes.clear();
    if (!diagnostic.is_object()) { Hide(); return; }
    auto const& issues = diagnostic.at("issues");
    bool completed = diagnostic.value("completed",false);
    heading->SetLabel(wxString::FromUTF8(completed
        ? "Forecast completed. Input recovery warnings (select a row for affected records):"
        : "Update failed; previous forecast retained. " + diagnostic.value("failure",std::string{})));
    heading->SetForegroundColour(completed ? wxColour(140,65,0) : wxColour(160,0,0));
    int infoTypes = 0;
    for (auto const& issue : issues) {
        bool information = issue.value("routine",false);
        if (information) { ++infoTypes; if (!routine->GetValue()) continue; }
        long row = rows->InsertItem(rows->GetItemCount(),wxString::FromUTF8(issue.at("label").get<std::string>()));
        rows->SetItem(row,1,wxString::Format("%d",issue.at("affected_records").get<int>()));
        rows->SetItem(row,2,wxString::Format("%d",issue.at("affected_districts").get<int>()));
        rows->SetItem(row,3,wxString::FromUTF8(issue.at("district").get<std::string>() + ": " + issue.at("record").get<std::string>()));
        rows->SetItem(row,4,wxString::FromUTF8(issue.at("action").get<std::string>()));
        if (!information) rows->SetItemTextColour(row,wxColour(145,40,0));
        displayedTypes.push_back(issue.at("type"));
    }
    routine->SetLabel(wxString::Format("Show routine incomplete-count information (%d issue types)",infoTypes));
    Show(!completed || !issues.empty()); Layout();
}

void LiveInputWarningPanel::showDetails(wxListEvent& event) {
    auto index = std::size_t(event.GetIndex());
    if (index >= displayedTypes.size()) return;
    if (!detailWindow) {
        detailWindow = new wxDialog(this,wxID_ANY,"Live input issue details",wxDefaultPosition,wxSize(820,460),wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        detailText = new wxTextCtrl(detailWindow,wxID_ANY,"",wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE | wxTE_READONLY);
        auto sizer = new wxBoxSizer(wxVERTICAL); sizer->Add(detailText,1,wxEXPAND | wxALL,6);
        detailWindow->SetSizer(sizer);
        detailWindow->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent&) {
            // This is a non-modal inspection window. Destroy it on close;
            // hiding abandoned dialogs would accumulate memory across replays.
            auto window = detailWindow;
            detailWindow = nullptr; detailText = nullptr;
            window->Destroy();
        });
    }
    std::ostringstream text;
    for (auto const& issue : details) if (issue.at("type") == displayedTypes[index]) {
        text << issue.at("district").get<std::string>() << " / " << issue.at("record").get<std::string>() << "\n"
            << issue.at("explanation").get<std::string>() << "\n"
            << "Rejected: " << countsText(issue.at("rejected")) << "\n"
            << "Effective: " << countsText(issue.at("effective")) << "\n"
            << issue.at("action").get<std::string>() << "\n";
        if (!issue.at("observed_at").get<std::string>().empty()) text << "Accepted observation time: " << issue.at("observed_at").get<std::string>() << "\n";
        text << "\n";
    }
    detailText->SetValue(wxString::FromUTF8(text.str()));
    detailWindow->Show(); detailWindow->Raise();
}
