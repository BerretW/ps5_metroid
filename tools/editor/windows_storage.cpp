#include "windows_storage.hpp"

#include <vector>

namespace echo::editor {

bool readContent(const std::wstring& path, std::string& result, std::wstring* finalPath,
                 DWORD& problem) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { problem = GetLastError(); return false; }
    LARGE_INTEGER size{};
    bool ok = GetFileType(file) == FILE_TYPE_DISK && GetFileSizeEx(file, &size) &&
              size.QuadPart > 0 && size.QuadPart <= 1024 * 1024;
    if (!ok) problem = ERROR_INVALID_DATA;
    if (ok && finalPath) {
        std::vector<wchar_t> buffer(32768);
        DWORD length = GetFinalPathNameByHandleW(file, buffer.data(),
                                                 static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
        if (length == 0 || length >= buffer.size()) {
            problem = length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
            ok = false;
        }
        else finalPath->assign(buffer.data(), length);
    }
    if (ok) {
        result.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read{};
        if (!ReadFile(file, result.data(), static_cast<DWORD>(result.size()), &read, nullptr)) {
            problem = GetLastError();
            ok = false;
        } else if (read != result.size()) {
            problem = ERROR_HANDLE_EOF;
            ok = false;
        }
    }
    if (!CloseHandle(file)) { problem = GetLastError(); ok = false; }
    return ok;
}

bool replaceContent(const std::wstring& path, std::string_view expected,
                    std::string_view replacement, std::wstring& error) {
    if (replacement.empty() || replacement.size() > 1024 * 1024) {
        error = L"Replacement content has an invalid size.";
        return false;
    }
    std::string current;
    std::wstring finalPath;
    DWORD code{};
    if (!readContent(path, current, &finalPath, code)) {
        error = L"Could not re-read the opened file (Windows error " + std::to_wstring(code) + L").";
        return false;
    }
    if (finalPath != path || current != expected) {
        error = L"The file changed outside the editor. Reopen it before saving.";
        return false;
    }
    std::wstring temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary = path + L".echo-" + std::to_wstring(GetCurrentProcessId()) +
                    L"-" + std::to_wstring(attempt) + L".tmp";
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Could not create a temporary file (Windows error " +
                std::to_wstring(GetLastError()) + L").";
        return false;
    }
    DWORD written{};
    bool ok = WriteFile(file, replacement.data(), static_cast<DWORD>(replacement.size()), &written, nullptr) &&
              written == replacement.size() && FlushFileBuffers(file);
    code = ok ? 0 : GetLastError();
    if (!CloseHandle(file)) { code = GetLastError(); ok = false; }
    if (ok && !ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)) {
        code = GetLastError();
        ok = false;
    }
    if (!ok) {
        if (!DeleteFileW(temporary.c_str())) {
            error = L"Save failed and the temporary file could not be removed:\n" + temporary;
        } else {
            error = L"Could not replace the file (Windows error " + std::to_wstring(code) + L").";
        }
        return false;
    }
    error.clear();
    return true;
}

} // namespace echo::editor
