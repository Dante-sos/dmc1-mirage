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
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    DWORD size = GetFileSize(file, nullptr);
    std::string text(size, '\0');
    DWORD read = 0;
    ReadFile(file, text.data(), size, &read, nullptr);
    CloseHandle(file);
    text.resize(read);
    return text;
}

void collect_paths(const std::string& text, std::vector<std::wstring>& out) {
    const std::string key = "\"path\"";
    size_t at = 0;
    while ((at = text.find(key, at)) != std::string::npos) {
        at = text.find('"', at + key.size());
        if (at == std::string::npos) {
            break;
        }
        size_t end = text.find('"', at + 1);
        if (end == std::string::npos) {
            break;
        }
        std::string raw = text.substr(at + 1, end - at - 1);
        std::wstring wide;
        wide.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
                wide.push_back(L'\\');
                ++i;
            } else {
                wide.push_back(static_cast<unsigned char>(raw[i]));
            }
        }
        out.push_back(wide);
        at = end + 1;
    }
}

bool write_bytes(const std::wstring& path, const unsigned char* data, unsigned size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(file, data, size, &wrote, nullptr);
    CloseHandle(file);
