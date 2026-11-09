#include "core/csv.h"

#include <cwctype>

namespace obex {

namespace {

std::wstring Lower(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

std::wstring Quote(const std::wstring& field) {
    if (field.find_first_of(L",\"\r\n") == std::wstring::npos) return field;
    std::wstring quoted = L"\"";
    for (wchar_t c : field) {
        if (c == L'"') quoted += L'"';
        quoted += c;
    }
    return quoted + L"\"";
}

}  // namespace

std::vector<std::vector<std::wstring>> ParseCsv(std::wstring_view text) {
    std::vector<std::vector<std::wstring>> rows;
    std::vector<std::wstring> row;
    std::wstring field;
    bool quoted = false;
    bool any = false;
    if (!text.empty() && text[0] == 0xFEFF) text.remove_prefix(1);
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (quoted) {
            if (c == L'"' && i + 1 < text.size() && text[i + 1] == L'"') {
                field += L'"';
                ++i;
            } else if (c == L'"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == L'"') {
            quoted = true;
            any = true;
        } else if (c == L',') {
            row.push_back(std::move(field));
            field.clear();
            any = true;
        } else if (c == L'\r' || c == L'\n') {
            if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            if (any || !field.empty()) {
                row.push_back(std::move(field));
                rows.push_back(std::move(row));
            }
            row.clear();
            field.clear();
            any = false;
        } else {
            field += c;
            any = true;
        }
    }
    if (any || !field.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    return rows;
}

bool ImportBrowserCsv(std::wstring_view text, std::vector<Entry>& entries) {
    const auto rows = ParseCsv(text);
    if (rows.empty()) return false;
    int name = -1, url = -1, user = -1, pass = -1, note = -1;
    for (int i = 0; i < static_cast<int>(rows[0].size()); ++i) {
        const std::wstring column = Lower(rows[0][i]);
        if (column == L"name" || column == L"title") name = i;
        else if (column == L"url" || column == L"origin" || column == L"website") url = i;
        else if (column == L"username" || column == L"login") user = i;
        else if (column == L"password") pass = i;
        else if (column == L"note" || column == L"notes") note = i;
    }
    if (pass < 0) return false;
    const auto cell = [](const std::vector<std::wstring>& row, int index) {
        return index >= 0 && index < static_cast<int>(row.size()) ? row[index] : std::wstring();
    };
    for (size_t r = 1; r < rows.size(); ++r) {
        Entry e;
        e.name = cell(rows[r], name);
        e.url = cell(rows[r], url);
        e.username = cell(rows[r], user);
        e.password = cell(rows[r], pass);
        e.notes = cell(rows[r], note);
        if (e.password.empty() && e.username.empty()) continue;
        if (e.name.empty()) e.name = e.url;
        entries.push_back(std::move(e));
    }
    return true;
}

std::wstring ExportBrowserCsv(const std::vector<Entry>& entries) {
    std::wstring csv = L"name,url,username,password,note\r\n";
    for (const Entry& e : entries) {
        csv += Quote(e.name) + L',' + Quote(e.url) + L',' + Quote(e.username) + L',' + Quote(e.password) + L',' + Quote(e.notes) + L"\r\n";
    }
    return csv;
}

}  // namespace obex
