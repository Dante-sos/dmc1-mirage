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
    return ok && wrote == size;
}

bool load_sibling(const wchar_t* name, std::vector<unsigned char>& bytes) {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring path = join(parent_of(self), name);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD size = GetFileSize(file, nullptr);
    bytes.resize(size);
    DWORD read = 0;
    ReadFile(file, bytes.data(), size, &read, nullptr);
    CloseHandle(file);
    bytes.resize(read);
    return read == size && size > 0;
}

std::wstring find_game() {
    std::vector<std::wstring> libraries;
    std::wstring steam = reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty()) {
        steam = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    }
    if (!steam.empty()) {
        libraries.push_back(steam);
        collect_paths(read_file(join(steam, L"steamapps\\libraryfolders.vdf")), libraries);
    }
    libraries.push_back(L"C:\\Program Files (x86)\\Steam");
    for (const std::wstring& library : libraries) {
        std::wstring exe = join(library, L"steamapps\\common\\Devil May Cry HD Collection\\dmc1.exe");
        if (exists(exe)) {
            return exe;
        }
    }
    wchar_t chosen[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Devil May Cry 1\0dmc1.exe\0";
    dialog.lpstrFile = chosen;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Select dmc1.exe";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return L"";
    }
    return chosen;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::wstring exe = find_game();
    if (exe.empty()) {
        MessageBoxW(nullptr, L"Could not find dmc1.exe.", L"DMC1 Mirage", MB_ICONERROR);
        return 1;
    }
    const unsigned char* dll = nullptr;
    unsigned dll_size = 0;
    const unsigned char* background = nullptr;
    unsigned background_size = 0;
#ifdef DMC1_EMBEDDED
    dll = kDll;
    dll_size = kDllSize;
    background = kBackground;
    background_size = kBackgroundSize;
#else
    std::vector<unsigned char> dll_bytes;
    std::vector<unsigned char> background_bytes;
    if (!load_sibling(L"XINPUT9_1_0.dll", dll_bytes)) {
        MessageBoxW(nullptr, L"XINPUT9_1_0.dll was not found next to this installer.", L"DMC1 Mirage", MB_ICONERROR);
        return 1;
    }
    if (!load_sibling(L"launcher-bg.jpg", background_bytes)) {
        load_sibling(L"dmc1-mission-bg.jpg", background_bytes);
    }
    dll = dll_bytes.data();
    dll_size = static_cast<unsigned>(dll_bytes.size());
    if (!background_bytes.empty()) {
        background = background_bytes.data();
        background_size = static_cast<unsigned>(background_bytes.size());
    }
#endif
    std::wstring dir = parent_of(exe);
    std::wstring dll_path = join(dir, L"XINPUT9_1_0.dll");
    if (!write_bytes(dll_path, dll, dll_size)) {
        MessageBoxW(nullptr, L"Close Devil May Cry and run this again. The game is using the mod file.", L"DMC1 Mirage", MB_ICONWARNING);
        return 1;
    }
    if (background && background_size > 0) {
        write_bytes(join(dir, L"launcher-bg.jpg"), background, background_size);
        write_bytes(join(dir, L"dmc1-mission-bg.jpg"), background, background_size);
    }
    std::wstring message = L"Installed for\n" + exe + L"\n\nRestart the game. This does not modify dmc1.exe.";
    MessageBoxW(nullptr, message.c_str(), L"DMC1 Mirage", MB_OK);
    return 0;
}
