#pragma once

#include "core/vault.h"

#include <string>
#include <string_view>
#include <vector>

namespace obex {

// RFC 4180: quoted fields may hold commas, quotes ("") and line breaks.
std::vector<std::vector<std::wstring>> ParseCsv(std::wstring_view text);

// The password export of Chrome and Edge: name,url,username,password,note.
// Columns are found by header, so the order and extra columns do not matter.
// Returns false if the header has no password column.
bool ImportBrowserCsv(std::wstring_view text, std::vector<Entry>& entries);
std::wstring ExportBrowserCsv(const std::vector<Entry>& entries);

}  // namespace obex
