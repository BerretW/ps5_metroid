#pragma once

#include <windows.h>
#include <string>
#include <string_view>

namespace echo::editor {

bool readContent(const std::wstring& path, std::string& result, std::wstring* finalPath,
                 DWORD& error);
bool replaceContent(const std::wstring& path, std::string_view expected,
                    std::string_view replacement, std::wstring& error);

} // namespace echo::editor
