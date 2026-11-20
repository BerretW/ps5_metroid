#include "echo/framework/data.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <shlobj.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string_view>

namespace echo::framework {
namespace {

constexpr std::size_t kPathCapacity = 1024;
constexpr DWORD kDirectoryShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    ~ScopedHandle() {
        if (valid()) {
            CloseHandle(handle_);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.release()) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            if (valid()) {
                CloseHandle(handle_);
            }
            handle_ = other.release();
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] HANDLE release() noexcept {
        const HANDLE handle = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return handle;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class FindHandle {
public:
    explicit FindHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    ~FindHandle() {
        if (valid()) {
            FindClose(handle_);
        }
    }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] bool append(std::array<wchar_t, kPathCapacity>& path,
                          std::size_t& length, std::wstring_view part) noexcept {
    if (part.size() >= path.size() - length) {
        return false;
    }
    std::memcpy(path.data() + length, part.data(), part.size() * sizeof(wchar_t));
    length += part.size();
    path[length] = L'\0';
    return true;
}

[[nodiscard]] bool validDirectoryName(std::wstring_view name) noexcept {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (const wchar_t character : name) {
        if (!((character >= L'a' && character <= L'z') ||
              (character >= L'A' && character <= L'Z') ||
              (character >= L'0' && character <= L'9') ||
              character == L'_' || character == L'-')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool currentUserOwns(HANDLE handle) noexcept {
    ScopedHandle token{};
    HANDLE tokenHandle = INVALID_HANDLE_VALUE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tokenHandle)) {
        return false;
    }
    token = ScopedHandle(tokenHandle);

    alignas(TOKEN_USER) std::array<std::byte, 1024> tokenBuffer{};
    DWORD tokenBytes{};
    if (!GetTokenInformation(token.get(), TokenUser, tokenBuffer.data(),
                             static_cast<DWORD>(tokenBuffer.size()), &tokenBytes)) {
        return false;
    }
    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(tokenBuffer.data());

    PSID ownerSid{};
    PSECURITY_DESCRIPTOR descriptor{};
    const DWORD securityResult = GetSecurityInfo(
        handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
        &ownerSid, nullptr, nullptr, nullptr, &descriptor);
    const bool isOwner = securityResult == ERROR_SUCCESS && ownerSid != nullptr &&
                         tokenUser->User.Sid != nullptr &&
                         EqualSid(ownerSid, tokenUser->User.Sid) != FALSE;
    if (descriptor != nullptr) {
        LocalFree(descriptor);
    }
    return isOwner;
}

[[nodiscard]] bool openOwnedDirectory(const wchar_t* path, bool create,
                                      HANDLE& output) noexcept {
    if (create && !CreateDirectoryW(path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    ScopedHandle directory(CreateFileW(
        path, FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_DELETE_CHILD |
                  FILE_READ_ATTRIBUTES | READ_CONTROL,
        kDirectoryShareMode, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!directory.valid()) {
        return false;
    }
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!GetFileInformationByHandleEx(directory.get(), FileAttributeTagInfo,
                                      &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        !currentUserOwns(directory.get())) {
        OutputDebugStringW(L"Echo save storage rejected an unsafe or non-owned directory.\n");
        return false;
    }
    output = directory.release();
    return true;
}

[[nodiscard]] bool pathFromHandle(HANDLE handle,
                                  std::array<wchar_t, kPathCapacity>& output) noexcept {
    const DWORD length = GetFinalPathNameByHandleW(
        handle, output.data(), static_cast<DWORD>(output.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length == 0 || length >= output.size()) {
        output[0] = L'\0';
        return false;
    }
    output[length] = L'\0';
    return true;
}

[[nodiscard]] bool samePath(const wchar_t* actual, const wchar_t* expected) noexcept {
    return CompareStringOrdinal(actual, -1, expected, -1, TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool buildSlotPath(const wchar_t* root, std::string_view slot,
                                 std::wstring_view extension,
                                 std::array<wchar_t, kPathCapacity>& output) noexcept {
    if (!SaveService::validSlot(slot)) {
        return false;
    }
    std::size_t length = std::wcslen(root);
    if (length >= output.size()) {
        return false;
    }
    if (1 + slot.size() + extension.size() >= output.size() - length) {
        return false;
    }
    std::memcpy(output.data(), root, (length + 1) * sizeof(wchar_t));
    if (!append(output, length, L"\\")) {
        return false;
    }
    for (const char character : slot) {
        output[length++] = static_cast<wchar_t>(static_cast<unsigned char>(character));
    }
    output[length] = L'\0';
    return append(output, length, extension);
}

[[nodiscard]] bool regularOwnedFile(HANDLE handle, const wchar_t* expectedPath) noexcept {
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    std::array<wchar_t, kPathCapacity> actualPath{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
                                      &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        !pathFromHandle(handle, actualPath) || !samePath(actualPath.data(), expectedPath) ||
        !currentUserOwns(handle)) {
        OutputDebugStringW(L"Echo save storage rejected an unsafe or non-owned save file.\n");
        return false;
    }
    return true;
}

[[nodiscard]] bool discardCreatedFile(HANDLE handle) noexcept {
    FILE_DISPOSITION_INFO disposition{TRUE};
    const bool removed = SetFileInformationByHandle(
        handle, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
    CloseHandle(handle);
    if (!removed) {
        OutputDebugStringW(L"Echo save storage could not remove its temporary file handle.\n");
    }
    return removed;
}

[[nodiscard]] bool toSlotName(std::wstring_view fileName,
                              std::array<char, 33>& slot) noexcept {
    constexpr std::wstring_view extension = L".sav";
    if (fileName.size() <= extension.size() ||
        CompareStringOrdinal(fileName.data() + fileName.size() - extension.size(),
                             static_cast<int>(extension.size()), extension.data(),
                             static_cast<int>(extension.size()), TRUE) != CSTR_EQUAL) {
        return false;
    }
    const std::size_t slotLength = fileName.size() - extension.size();
    if (slotLength > 32) {
        return false;
    }
    for (std::size_t index = 0; index < slotLength; ++index) {
        if (fileName[index] > 0x7f) {
            return false;
        }
        slot[index] = static_cast<char>(fileName[index]);
    }
    slot[slotLength] = '\0';
    return SaveService::validSlot(std::string_view(slot.data(), slotLength));
}

} // namespace

WindowsSaveStorage::~WindowsSaveStorage() {
    reset();
}

Status WindowsSaveStorage::initialize(std::wstring_view subdirectory) noexcept {
    if (rootHandle_ != nullptr) {
        return {ErrorCode::InvalidState, {}, "Windows save storage is already initialized."};
    }
    if (!validDirectoryName(subdirectory)) {
        return {ErrorCode::InvalidArgument, {}, "Save directory must be one safe path segment."};
    }

    PWSTR localAppData{};
    const HRESULT folderStatus = SHGetKnownFolderPath(
        FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData);
    if (FAILED(folderStatus) || localAppData == nullptr) {
        if (localAppData != nullptr) {
            CoTaskMemFree(localAppData);
        }
        return {ErrorCode::StorageFailure, {}, "Windows Local AppData directory is unavailable."};
    }

    std::array<wchar_t, kPathCapacity> appPath{};
    std::array<wchar_t, kPathCapacity> storagePath{};
    HANDLE localAppDataHandle = INVALID_HANDLE_VALUE;
    if (!openOwnedDirectory(localAppData, false, localAppDataHandle)) {
        CoTaskMemFree(localAppData);
        return {ErrorCode::StorageFailure, {}, "Windows Local AppData directory is unsafe."};
    }
    const bool resolvedLocalAppData =
        pathFromHandle(localAppDataHandle, appPath);
    CloseHandle(localAppDataHandle);
    CoTaskMemFree(localAppData);
    if (!resolvedLocalAppData) {
        return {ErrorCode::StorageFailure, {}, "Windows Local AppData directory could not be resolved."};
    }

    std::size_t appLength = std::wcslen(appPath.data());
    if (!append(appPath, appLength, L"\\PROJECT_ECHO")) {
        return {ErrorCode::CapacityExceeded, {}, "Windows save directory path is too long."};
    }

    HANDLE appHandle = INVALID_HANDLE_VALUE;
    if (!openOwnedDirectory(appPath.data(), true, appHandle)) {
        return {ErrorCode::StorageFailure, {}, "PROJECT_ECHO save directory is unsafe or unavailable."};
    }
    CloseHandle(appHandle);

    std::size_t storageLength = appLength;
    std::memcpy(storagePath.data(), appPath.data(), (storageLength + 1) * sizeof(wchar_t));
    if (!append(storagePath, storageLength, L"\\") ||
        !append(storagePath, storageLength, subdirectory)) {
        return {ErrorCode::CapacityExceeded, {}, "Windows save directory path is too long."};
    }

    HANDLE root = INVALID_HANDLE_VALUE;
    if (!openOwnedDirectory(storagePath.data(), true, root)) {
        return {ErrorCode::StorageFailure, {}, "Windows save directory is unsafe or unavailable."};
    }
    std::array<wchar_t, kPathCapacity> canonicalPath{};
    if (!pathFromHandle(root, canonicalPath)) {
        CloseHandle(root);
        return {ErrorCode::StorageFailure, {}, "Windows save directory could not be resolved."};
    }
    std::memcpy(rootPath_.data(), canonicalPath.data(),
                (std::wcslen(canonicalPath.data()) + 1) * sizeof(wchar_t));
    rootHandle_ = root;
    return Status::success();
}

SaveStorage WindowsSaveStorage::callbacks() noexcept {
    return {this, &readCallback, &writeTemporaryCallback, &replaceCallback,
            &removeTemporaryCallback, &enumerateCallback};
}

bool WindowsSaveStorage::readCallback(void* context, std::string_view slot,
                                      char* output, std::size_t capacity,
                                      std::size_t& size) noexcept {
    return context != nullptr &&
           static_cast<WindowsSaveStorage*>(context)->read(slot, output, capacity, size);
}

bool WindowsSaveStorage::writeTemporaryCallback(void* context, std::string_view slot,
                                                const char* data, std::size_t size) noexcept {
    return context != nullptr &&
           static_cast<WindowsSaveStorage*>(context)->writeTemporary(slot, data, size);
}

bool WindowsSaveStorage::replaceCallback(void* context, std::string_view slot) noexcept {
    return context != nullptr && static_cast<WindowsSaveStorage*>(context)->replace(slot);
}

bool WindowsSaveStorage::removeTemporaryCallback(void* context,
                                                 std::string_view slot) noexcept {
    return context != nullptr &&
           static_cast<WindowsSaveStorage*>(context)->removeTemporary(slot);
}

bool WindowsSaveStorage::enumerateCallback(void* context, char* output,
                                           std::size_t capacity,
                                           std::size_t& written) noexcept {
    return context != nullptr &&
           static_cast<WindowsSaveStorage*>(context)->enumerate(output, capacity, written);
}

bool WindowsSaveStorage::read(std::string_view slot, char* output,
                              std::size_t capacity, std::size_t& size) noexcept {
    size = 0;
    if (rootHandle_ == nullptr || output == nullptr || capacity == 0 ||
        capacity > kMaxSaveBytes) {
        return false;
    }
    std::array<wchar_t, kPathCapacity> path{};
    if (!buildSlotPath(rootPath_.data(), slot, L".sav", path)) {
        return false;
    }
    ScopedHandle file(CreateFileW(path.data(), GENERIC_READ | READ_CONTROL,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.valid() || !regularOwnedFile(file.get(), path.data())) {
        return false;
    }
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file.get(), &fileSize) || fileSize.QuadPart <= 0 ||
        static_cast<unsigned long long>(fileSize.QuadPart) > capacity) {
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(fileSize.QuadPart);
    std::size_t offset = 0;
    while (offset < expected) {
        const DWORD chunk = static_cast<DWORD>(expected - offset);
        DWORD received{};
        if (!ReadFile(file.get(), output + offset, chunk, &received, nullptr) || received == 0) {
            return false;
        }
        offset += received;
    }
    size = expected;
    return true;
}

bool WindowsSaveStorage::writeTemporary(std::string_view slot,
                                        const char* data, std::size_t size) noexcept {
    if (rootHandle_ == nullptr || temporaryHandle_ != nullptr ||
        !SaveService::validSlot(slot) || data == nullptr || size == 0 ||
        size > kMaxSaveBytes) {
        return false;
    }
    GUID id{};
    wchar_t guidText[40]{};
    if (FAILED(CoCreateGuid(&id)) || StringFromGUID2(id, guidText, 40) == 0) {
        return false;
    }
    std::array<wchar_t, kPathCapacity> path{};
    std::size_t length = std::wcslen(rootPath_.data());
    if (length >= path.size()) {
        return false;
    }
    std::memcpy(path.data(), rootPath_.data(), (length + 1) * sizeof(wchar_t));
    if (!append(path, length, L"\\.tmp-") ||
        !append(path, length, std::wstring_view(guidText))) {
        return false;
    }

    ScopedHandle file(CreateFileW(path.data(), GENERIC_WRITE | DELETE | READ_CONTROL,
                                  FILE_SHARE_READ, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT,
                                  nullptr));
    if (!file.valid()) {
        return false;
    }
    if (!regularOwnedFile(file.get(), path.data())) {
        (void)discardCreatedFile(file.release());
        return false;
    }
    std::size_t offset = 0;
    while (offset < size) {
        const DWORD chunk = static_cast<DWORD>(size - offset);
        DWORD written{};
        if (!WriteFile(file.get(), data + offset, chunk, &written, nullptr) || written == 0) {
            (void)discardCreatedFile(file.release());
            return false;
        }
        offset += written;
    }
    if (!FlushFileBuffers(file.get())) {
        (void)discardCreatedFile(file.release());
        return false;
    }
    temporaryHandle_ = file.release();
    std::memcpy(temporarySlot_.data(), slot.data(), slot.size());
    temporarySlot_[slot.size()] = '\0';
    return true;
}

bool WindowsSaveStorage::replace(std::string_view slot) noexcept {
    if (rootHandle_ == nullptr || temporaryHandle_ == nullptr ||
        std::string_view(temporarySlot_.data()) != slot ||
        !SaveService::validSlot(slot)) {
        return false;
    }
    std::array<wchar_t, kPathCapacity> targetPath{};
    if (!buildSlotPath(rootPath_.data(), slot, L".sav", targetPath)) {
        return false;
    }

    ScopedHandle existing(CreateFileW(targetPath.data(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (existing.valid()) {
        if (!regularOwnedFile(existing.get(), targetPath.data())) {
            return false;
        }
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND &&
               GetLastError() != ERROR_PATH_NOT_FOUND) {
        return false;
    }
    existing = ScopedHandle{};

    alignas(FILE_RENAME_INFO) std::array<std::byte, sizeof(FILE_RENAME_INFO) +
                                                       kPathCapacity * sizeof(wchar_t)> renameBuffer{};
    auto* renameInfo = reinterpret_cast<FILE_RENAME_INFO*>(renameBuffer.data());
    renameInfo->ReplaceIfExists = TRUE;
    renameInfo->RootDirectory = nullptr;
    renameInfo->FileNameLength =
        static_cast<DWORD>(std::wcslen(targetPath.data()) * sizeof(wchar_t));
    std::memcpy(renameInfo->FileName, targetPath.data(), renameInfo->FileNameLength);
    const std::size_t renameSize =
        offsetof(FILE_RENAME_INFO, FileName) + renameInfo->FileNameLength;
    if (!SetFileInformationByHandle(static_cast<HANDLE>(temporaryHandle_),
                                    FileRenameInfo, renameInfo,
                                    static_cast<DWORD>(renameSize))) {
        OutputDebugStringW(L"Echo save storage could not atomically replace a save slot.\n");
        return false;
    }
    CloseHandle(static_cast<HANDLE>(temporaryHandle_));
    temporaryHandle_ = nullptr;
    temporarySlot_.fill('\0');
    return true;
}

bool WindowsSaveStorage::removeTemporary(std::string_view slot) noexcept {
    if (temporaryHandle_ == nullptr || std::string_view(temporarySlot_.data()) != slot) {
        return false;
    }
    const bool removed = discardCreatedFile(static_cast<HANDLE>(temporaryHandle_));
    temporaryHandle_ = nullptr;
    temporarySlot_.fill('\0');
    return removed;
}

bool WindowsSaveStorage::enumerate(char* output, std::size_t capacity,
                                   std::size_t& written) noexcept {
    written = 0;
    if (rootHandle_ == nullptr || output == nullptr || capacity == 0 ||
        capacity > kMaxSaveBytes) {
        return false;
    }
    std::array<wchar_t, kPathCapacity> pattern{};
    std::size_t length = std::wcslen(rootPath_.data());
    if (length >= pattern.size()) {
        return false;
    }
    std::memcpy(pattern.data(), rootPath_.data(), (length + 1) * sizeof(wchar_t));
    if (!append(pattern, length, L"\\*.sav")) {
        return false;
    }

    WIN32_FIND_DATAW entry{};
    FindHandle search(FindFirstFileW(pattern.data(), &entry));
    if (!search.valid()) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    std::size_t resultSize = 0;
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        std::array<char, 33> slot{};
        const std::wstring_view fileName(entry.cFileName);
        if (!toSlotName(fileName, slot)) {
            OutputDebugStringW(L"Echo save storage ignored an invalid save-slot filename.\n");
            return false;
        }
        const std::size_t slotLength = std::strlen(slot.data());
        std::array<wchar_t, kPathCapacity> filePath{};
        if (!buildSlotPath(rootPath_.data(), std::string_view(slot.data()),
                           L".sav", filePath)) {
            return false;
        }
        ScopedHandle file(CreateFileW(filePath.data(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                      FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!file.valid() || !regularOwnedFile(file.get(), filePath.data())) {
            return false;
        }
        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file.get(), &fileSize) || fileSize.QuadPart <= 0 ||
            fileSize.QuadPart > static_cast<LONGLONG>(kMaxSaveBytes) ||
            slotLength + 1 > capacity - resultSize) {
            return false;
        }
        std::memcpy(output + resultSize, slot.data(), slotLength + 1);
        resultSize += slotLength + 1;
    } while (FindNextFileW(search.get(), &entry));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        return false;
    }
    written = resultSize;
    return true;
}

void WindowsSaveStorage::reset() noexcept {
    if (temporaryHandle_ != nullptr) {
        (void)discardCreatedFile(static_cast<HANDLE>(temporaryHandle_));
        temporaryHandle_ = nullptr;
    }
    temporarySlot_.fill('\0');
    if (rootHandle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(rootHandle_));
        rootHandle_ = nullptr;
    }
    rootPath_.fill(L'\0');
}

} // namespace echo::framework
