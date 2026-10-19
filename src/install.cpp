#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <cwchar>
#include <string>
#include <vector>

#ifdef DMC1_EMBEDDED
#include "payload.h"
#endif

namespace {

bool exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring join(const std::wstring& dir, const wchar_t* name) {
    if (dir.empty()) {
        return name;
    }
    wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') {
        return dir + name;
    }
    return dir + L"\\" + name;
}

std::wstring parent_of(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, slash);
}

std::wstring reg_string(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buffer[MAX_PATH]{};
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) {
        return L"";
    }
    return buffer;
}

std::string read_file(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
