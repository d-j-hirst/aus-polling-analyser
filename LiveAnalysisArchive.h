#pragma once

#include "json.h"

#include <string>

namespace LiveAnalysisArchive {

// Encode retained diagnostics only. The caller transfers its completed JSON
// document; identity/key packing and display precision never touch live state.
// The returned UTF-8 JSON is ready for gzip compression by the GUI exporter.
std::string serialize(nlohmann::json document);

}
