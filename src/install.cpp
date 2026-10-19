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
