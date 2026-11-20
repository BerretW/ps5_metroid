#include "echo/framework/data.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <string_view>

namespace {

int failures{};

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check(echo::framework::Status status, std::string_view message) {
    if (!status) {
        ++failures;
        std::cerr << "FAIL: " << message << " (" << status.detail << ")\n";
    }
}

void cleanupTestDirectory(std::wstring_view subdirectory) {
    PWSTR localAppData{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                    nullptr, &localAppData)) ||
        localAppData == nullptr) {
        return;
    }
    std::array<wchar_t, 1024> directory{};
    std::size_t length = std::wcslen(localAppData);
    constexpr std::wstring_view appFolder = L"\\PROJECT_ECHO\\";
    constexpr std::wstring_view saveName = L"\\slot_a.sav";
    if (length + appFolder.size() + subdirectory.size() + saveName.size() <
        directory.size()) {
        std::memcpy(directory.data(), localAppData, length * sizeof(wchar_t));
        std::memcpy(directory.data() + length, appFolder.data(),
                    appFolder.size() * sizeof(wchar_t));
        length += appFolder.size();
        std::memcpy(directory.data() + length, subdirectory.data(),
                    subdirectory.size() * sizeof(wchar_t));
        length += subdirectory.size();
        directory[length] = L'\0';
        std::array<wchar_t, 1024> savePath = directory;
        std::memcpy(savePath.data() + length, saveName.data(),
                    (saveName.size() + 1) * sizeof(wchar_t));
        (void)DeleteFileW(savePath.data());
        (void)RemoveDirectoryW(directory.data());
    }
    CoTaskMemFree(localAppData);
}

} // namespace

int main() {
    using namespace echo::framework;

    GUID id{};
    wchar_t guidText[40]{};
    check(SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(id, guidText, 40) != 0,
          "test directory ID is generated");
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    std::array<wchar_t, 40> subdirectory{};
    constexpr wchar_t prefix[] = L"Test_";
    std::memcpy(subdirectory.data(), prefix, sizeof(prefix) - sizeof(wchar_t));
    std::size_t subdirectoryLength = 5;
    for (const wchar_t character : std::wstring_view(guidText)) {
        if ((character >= L'0' && character <= L'9') ||
            (character >= L'a' && character <= L'f') ||
            (character >= L'A' && character <= L'F')) {
            subdirectory[subdirectoryLength++] = character;
        }
    }
    subdirectory[subdirectoryLength] = L'\0';

    bool saved = false;
    bool initializedProvider = false;
    {
        WindowsSaveStorage provider{};
        const Status initialized = provider.initialize(
            std::wstring_view(subdirectory.data(), subdirectoryLength));
        initializedProvider = static_cast<bool>(initialized);
        check(initializedProvider, "provider initializes under the current user's Local AppData");
        if (initialized) {
            const SaveStorage storage = provider.callbacks();
            GameState state{};
            check(state.set({StateScope::Global, {}, "test.value"},
                            StateValue::fromInteger(17)),
                  "fixture state is populated");
            constexpr std::array<ModuleVersion, 1> modules{{{"echo.test", 1, true}}};
            std::array<char, kMaxSaveBytes> scratch{};
            Status status = SaveService::save(storage, "slot_a", "echo.test", 1,
                                              modules, state, scratch.data(), scratch.size());
            check(status, "first save creates a temporary file and commits the slot");
            saved = static_cast<bool>(status);

            check(state.set({StateScope::Global, {}, "test.value"},
                            StateValue::fromInteger(42)),
                  "replacement fixture state is populated");
            status = SaveService::save(storage, "slot_a", "echo.test", 1,
                                       modules, state, scratch.data(), scratch.size());
            check(status, "replacement atomically installs the new slot");
            saved = saved || static_cast<bool>(status);

            GameState restored{};
            SaveHeader header{};
            status = SaveService::load(storage, "slot_a", "echo.test", 1, modules,
                                       restored, header, scratch.data(), scratch.size());
            StateValue loaded{};
            check(status && restored.get({StateScope::Global, {}, "test.value"}, loaded) &&
                      loaded.integer == 42,
                  "save loads and verifies the latest slot contents");

            std::array<char, 128> slots{};
            std::size_t slotsSize{};
            status = SaveService::enumerate(storage, slots.data(), slots.size(), slotsSize);
            check(status, "save-slot enumeration succeeds");
            check(slotsSize == 7 && std::string_view(slots.data(), 6) == "slot_a" &&
                      slots[6] == '\0',
                  "slot enumeration returns only validated save IDs");

            constexpr char abandoned[] = "incomplete";
            check(storage.writeTemporary(storage.context, "slot_b", abandoned,
                                         sizeof(abandoned) - 1) &&
                      storage.removeTemporary(storage.context, "slot_b"),
                  "failed save cleanup removes only the temporary file created by this provider");
            check(!storage.writeTemporary(storage.context, "../outside", abandoned,
                                          sizeof(abandoned) - 1),
                  "provider rejects path-like slot IDs independently");
        }
    }

    if (initializedProvider) {
        cleanupTestDirectory(std::wstring_view(subdirectory.data(), subdirectoryLength));
    }
    if (failures != 0) {
        std::cerr << failures << " Windows save storage test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Echo Framework Windows save storage tests passed\n";
    return EXIT_SUCCESS;
}
