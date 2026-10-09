#pragma once
#include "json.h"
#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>

// Persistent operator warnings belong beside the results, not in a series of
// modal prompts. Incomplete counting is optional information; repaired errors
// stay visible. Selecting a type opens its affected-record details nonmodally.
class LiveInputWarningPanel : public wxPanel {
public:
    explicit LiveInputWarningPanel(wxWindow* parent);
    void setDiagnostic(nlohmann::json diagnostic, nlohmann::json details);
private:
    void rebuildRows();
    void showDetails(wxListEvent& event);
    wxStaticText* heading;
    wxCheckBox* routine;
    wxListCtrl* rows;
    wxDialog* detailWindow = nullptr;
    wxTextCtrl* detailText = nullptr;
    nlohmann::json diagnostic, details;
    std::vector<std::string> displayedTypes;
};
