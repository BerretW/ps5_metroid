#include "echo/framework/data.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace echo::framework {
namespace {

constexpr char kParentDirectory[] = "/data/project_echo";

[[nodiscard]] bool validDirectoryName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 32) {
        return false;
    }
    for (const char character : name) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ensureDirectory(const char* path, bool requirePrivate) noexcept {
    if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat status{};
    if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode) ||
        S_ISLNK(status.st_mode) ||
        (requirePrivate && (status.st_mode & (S_IWGRP | S_IWOTH)) != 0)) {
        return false;
    }
    return true;
}

[[nodiscard]] bool makeSlotPath(const char* root, std::string_view slot,
                                const char* extension, char* output,
                                std::size_t capacity) noexcept {
    if (!SaveService::validSlot(slot)) {
        return false;
    }
    const int count = std::snprintf(output, capacity, "%s/%.*s%s", root,
                                    static_cast<int>(slot.size()), slot.data(), extension);
    return count > 0 && static_cast<std::size_t>(count) < capacity;
}

[[nodiscard]] bool regularFile(const char* path, struct stat& status) noexcept {
    if (lstat(path, &status) != 0) {
        return false;
    }
    return S_ISREG(status.st_mode) && !S_ISLNK(status.st_mode);
}

[[nodiscard]] bool regularDescriptor(int descriptor, const struct stat& pathStatus) noexcept {
    struct stat openedStatus{};
    return fstat(descriptor, &openedStatus) == 0 &&
           S_ISREG(openedStatus.st_mode) && !S_ISLNK(openedStatus.st_mode) &&
           openedStatus.st_dev == pathStatus.st_dev &&
           openedStatus.st_ino == pathStatus.st_ino;
}

[[nodiscard]] bool copyDirectorySlot(const char* name, std::array<char, 33>& slot) noexcept {
    const std::string_view fileName{name};
    constexpr std::string_view extension = ".sav";
    if (fileName.size() <= extension.size() ||
        fileName.substr(fileName.size() - extension.size()) != extension) {
        return false;
    }
    const std::size_t slotLength = fileName.size() - extension.size();
    if (slotLength > 32) {
        return false;
    }
    std::memcpy(slot.data(), fileName.data(), slotLength);
    slot[slotLength] = '\0';
    return SaveService::validSlot(std::string_view(slot.data(), slotLength));
}

} // namespace

Ps5SaveStorage::~Ps5SaveStorage() {
    reset();
}

Status Ps5SaveStorage::initialize(std::string_view subdirectory) noexcept {
    if (rootPath_[0] != '\0') {
        return {ErrorCode::InvalidState, {}, "PS5 save storage is already initialized."};
    }
    if (!validDirectoryName(subdirectory)) {
        return {ErrorCode::InvalidArgument, {}, "Save directory must be one safe path segment."};
    }
    if (!ensureDirectory(kParentDirectory, false)) {
        return {ErrorCode::StorageFailure, {}, "PS5 application data directory is unsafe or unavailable."};
    }
    const int count = std::snprintf(rootPath_.data(), rootPath_.size(),
                                    "%s/%.*s", kParentDirectory,
                                    static_cast<int>(subdirectory.size()), subdirectory.data());
    if (count <= 0 || static_cast<std::size_t>(count) >= rootPath_.size()) {
        rootPath_.fill('\0');
        return {ErrorCode::CapacityExceeded, {}, "PS5 save directory path exceeds its limit."};
    }
    if (!ensureDirectory(rootPath_.data(), true)) {
        rootPath_.fill('\0');
        return {ErrorCode::StorageFailure, {}, "PS5 save directory is unsafe or unavailable."};
    }
    return Status::success();
}

SaveStorage Ps5SaveStorage::callbacks() noexcept {
    return {this, &readCallback, &writeTemporaryCallback, &replaceCallback,
            &removeTemporaryCallback, &enumerateCallback};
}

bool Ps5SaveStorage::readCallback(void* context, std::string_view slot,
                                  char* output, std::size_t capacity,
                                  std::size_t& size) noexcept {
    return context != nullptr &&
           static_cast<Ps5SaveStorage*>(context)->read(slot, output, capacity, size);
}

bool Ps5SaveStorage::writeTemporaryCallback(void* context, std::string_view slot,
                                            const char* data, std::size_t size) noexcept {
    return context != nullptr &&
           static_cast<Ps5SaveStorage*>(context)->writeTemporary(slot, data, size);
}

bool Ps5SaveStorage::replaceCallback(void* context, std::string_view slot) noexcept {
    return context != nullptr && static_cast<Ps5SaveStorage*>(context)->replace(slot);
}

bool Ps5SaveStorage::removeTemporaryCallback(void* context,
                                             std::string_view slot) noexcept {
    return context != nullptr &&
           static_cast<Ps5SaveStorage*>(context)->removeTemporary(slot);
}

bool Ps5SaveStorage::enumerateCallback(void* context, char* output,
                                       std::size_t capacity,
                                       std::size_t& written) noexcept {
    return context != nullptr &&
           static_cast<Ps5SaveStorage*>(context)->enumerate(output, capacity, written);
}

bool Ps5SaveStorage::read(std::string_view slot, char* output,
                          std::size_t capacity, std::size_t& size) noexcept {
    size = 0;
    if (rootPath_[0] == '\0' || output == nullptr || capacity == 0 ||
        capacity > kMaxSaveBytes) {
        return false;
    }
    char path[160]{};
    if (!makeSlotPath(rootPath_.data(), slot, ".sav", path, sizeof(path))) {
        return false;
    }
    struct stat pathStatus{};
    if (!regularFile(path, pathStatus) || pathStatus.st_size <= 0 ||
        static_cast<unsigned long long>(pathStatus.st_size) > capacity) {
        return false;
    }
    const int descriptor = open(path, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0) {
        return false;
    }
    if (!regularDescriptor(descriptor, pathStatus)) {
        (void)close(descriptor);
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(pathStatus.st_size);
    std::size_t offset = 0;
    while (offset < expected) {
        const ssize_t count = ::read(descriptor, output + offset, expected - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0 || static_cast<std::size_t>(count) > expected - offset) {
            (void)close(descriptor);
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    const bool closed = close(descriptor) == 0;
    if (!closed) {
        return false;
    }
    size = expected;
    return true;
}

bool Ps5SaveStorage::writeTemporary(std::string_view slot,
                                    const char* data, std::size_t size) noexcept {
    if (rootPath_[0] == '\0' || temporaryPath_[0] != '\0' ||
        !SaveService::validSlot(slot) || data == nullptr || size == 0 ||
        size > kMaxSaveBytes) {
        return false;
    }
    timespec currentTime{};
    if (clock_gettime(CLOCK_REALTIME, &currentTime) != 0) {
        return false;
    }

    int descriptor = -1;
    for (std::size_t attempt = 0; attempt < 8; ++attempt) {
        ++temporarySequence_;
        char name[48]{};
        const int nameLength = std::snprintf(
            name, sizeof(name), ".tmp-%llx-%08x",
            static_cast<unsigned long long>(currentTime.tv_sec), temporarySequence_);
        if (nameLength <= 0 || static_cast<std::size_t>(nameLength) >= sizeof(name)) {
            return false;
        }
        const int pathLength = std::snprintf(temporaryPath_.data(), temporaryPath_.size(),
                                             "%s/%s", rootPath_.data(), name);
        if (pathLength <= 0 ||
            static_cast<std::size_t>(pathLength) >= temporaryPath_.size()) {
            temporaryPath_.fill('\0');
            return false;
        }
        descriptor = open(temporaryPath_.data(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (descriptor >= 0 || errno != EEXIST) {
            break;
        }
        temporaryPath_.fill('\0');
    }
    if (descriptor < 0) {
        temporaryPath_.fill('\0');
        return false;
    }

    struct stat createdStatus{};
    if (fstat(descriptor, &createdStatus) != 0 || !S_ISREG(createdStatus.st_mode)) {
        (void)close(descriptor);
        (void)unlink(temporaryPath_.data());
        temporaryPath_.fill('\0');
        return false;
    }
    std::size_t offset = 0;
    bool writeSucceeded = true;
    while (offset < size) {
        const ssize_t count = write(descriptor, data + offset, size - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0 || static_cast<std::size_t>(count) > size - offset) {
            writeSucceeded = false;
            break;
        }
        offset += static_cast<std::size_t>(count);
    }
    if (writeSucceeded && fsync(descriptor) != 0) {
        writeSucceeded = false;
    }
    const bool closeSucceeded = close(descriptor) == 0;
    if (!writeSucceeded || !closeSucceeded) {
        (void)unlink(temporaryPath_.data());
        temporaryPath_.fill('\0');
        return false;
    }
    std::memcpy(temporarySlot_.data(), slot.data(), slot.size());
    temporarySlot_[slot.size()] = '\0';
    return true;
}

bool Ps5SaveStorage::replace(std::string_view slot) noexcept {
    if (rootPath_[0] == '\0' || temporaryPath_[0] == '\0' ||
        std::string_view(temporarySlot_.data()) != slot ||
        !SaveService::validSlot(slot)) {
        return false;
    }
    char targetPath[160]{};
    if (!makeSlotPath(rootPath_.data(), slot, ".sav", targetPath, sizeof(targetPath))) {
        return false;
    }
    struct stat existing{};
    if (lstat(targetPath, &existing) == 0) {
        if (!S_ISREG(existing.st_mode) || S_ISLNK(existing.st_mode)) {
            return false;
        }
    } else if (errno != ENOENT) {
        return false;
    }
    if (rename(temporaryPath_.data(), targetPath) != 0) {
        return false;
    }
    temporaryPath_.fill('\0');
    temporarySlot_.fill('\0');
    return true;
}

bool Ps5SaveStorage::removeTemporary(std::string_view slot) noexcept {
    if (temporaryPath_[0] == '\0' ||
        std::string_view(temporarySlot_.data()) != slot) {
        return false;
    }
    struct stat existing{};
    if (lstat(temporaryPath_.data(), &existing) != 0) {
        const bool absent = errno == ENOENT;
        if (absent) {
            temporaryPath_.fill('\0');
            temporarySlot_.fill('\0');
        }
        return absent;
    }
    if (!S_ISREG(existing.st_mode) || S_ISLNK(existing.st_mode) ||
        unlink(temporaryPath_.data()) != 0) {
        return false;
    }
    temporaryPath_.fill('\0');
    temporarySlot_.fill('\0');
    return true;
}

bool Ps5SaveStorage::enumerate(char* output, std::size_t capacity,
                               std::size_t& written) noexcept {
    written = 0;
    if (rootPath_[0] == '\0' || output == nullptr || capacity == 0 ||
        capacity > kMaxSaveBytes) {
        return false;
    }
    DIR* directory = opendir(rootPath_.data());
    if (directory == nullptr) {
        return false;
    }
    std::size_t resultSize = 0;
    bool valid = true;
    int directoryError = 0;
    for (;;) {
        errno = 0;
        dirent* entry = readdir(directory);
        if (entry == nullptr) {
            directoryError = errno;
            break;
        }
        std::array<char, 33> slot{};
        if (!copyDirectorySlot(entry->d_name, slot)) {
            const std::string_view name(entry->d_name);
            if (name.size() >= 4 && name.substr(name.size() - 4) == ".sav") {
                valid = false;
                break;
            }
            continue;
        }
        char path[160]{};
        struct stat status{};
        const std::size_t slotLength = std::strlen(slot.data());
        if (!makeSlotPath(rootPath_.data(), std::string_view(slot.data(), slotLength),
                          ".sav", path, sizeof(path)) ||
            !regularFile(path, status) || status.st_size <= 0 ||
            static_cast<unsigned long long>(status.st_size) > kMaxSaveBytes ||
            slotLength + 1 > capacity - resultSize) {
            valid = false;
            break;
        }
        std::memcpy(output + resultSize, slot.data(), slotLength + 1);
        resultSize += slotLength + 1;
    }
    const bool closeSucceeded = closedir(directory) == 0;
    if (!valid || directoryError != 0 || !closeSucceeded) {
        return false;
    }
    written = resultSize;
    return true;
}

void Ps5SaveStorage::reset() noexcept {
    if (temporaryPath_[0] != '\0') {
        (void)removeTemporary(std::string_view(temporarySlot_.data()));
    }
    rootPath_.fill('\0');
    temporaryPath_.fill('\0');
    temporarySlot_.fill('\0');
}

} // namespace echo::framework
