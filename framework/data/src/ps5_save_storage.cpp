#include "echo/framework/data.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <new>

extern "C" {
int sceKernelGetdents(int fd, char* buffer, int size);
int sceKernelGetdirentries(int fd, char* buffer, int size, long* base);
}

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

enum class PathKind : std::uint8_t { Missing, Directory, Regular, Symlink, Other, Error };

struct PathStatus {
    PathKind kind{PathKind::Error};
    struct stat status{};
    const char* method{"none"};
    int lstatError{};
    int openError{};
};

[[nodiscard]] PathKind kindFromMode(mode_t mode) noexcept {
    if (S_ISLNK(mode)) {
        return PathKind::Symlink;
    }
    if (S_ISDIR(mode)) {
        return PathKind::Directory;
    }
    return S_ISREG(mode) ? PathKind::Regular : PathKind::Other;
}

// lstat is tried first; if the app sandbox rejects it, an O_NOFOLLOW open plus
// fstat gives the same no-follow guarantee because a symlink fails with ELOOP.
[[nodiscard]] PathStatus inspectPath(const char* path, bool expectDirectory) noexcept {
    PathStatus result{};
    errno = 0;
    if (lstat(path, &result.status) == 0) {
        result.kind = kindFromMode(result.status.st_mode);
        result.method = "lstat";
        return result;
    }
    result.lstatError = errno;
    if (result.lstatError == ENOENT) {
        result.kind = PathKind::Missing;
        result.method = "lstat";
        return result;
    }
    errno = 0;
    const int flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | (expectDirectory ? O_DIRECTORY : 0);
    const int descriptor = open(path, flags);
    if (descriptor < 0) {
        result.openError = errno;
        result.method = "open";
        if (result.openError == ENOENT) {
            result.kind = PathKind::Missing;
        } else if (result.openError == ELOOP || result.openError == EMLINK) {
            result.kind = PathKind::Symlink;
        } else if (result.openError == ENOTDIR) {
            result.kind = PathKind::Other;
        }
        return result;
    }
    result.method = "open+fstat";
    if (fstat(descriptor, &result.status) == 0) {
        result.kind = kindFromMode(result.status.st_mode);
    } else {
        result.openError = errno;
    }
    (void)close(descriptor);
    return result;
}

[[nodiscard]] Status ensureDirectory(const char* path, bool requirePrivate,
                                     std::array<char, 512>& diagnostics) noexcept {
    std::size_t used = std::strlen(diagnostics.data());
    errno = 0;
    const int mkdirResult = mkdir(path, 0700);
    const int mkdirError = mkdirResult == 0 ? 0 : errno;
    const PathStatus inspected = inspectPath(path, true);
    bool privateMode = (inspected.status.st_mode & (S_IWGRP | S_IWOTH)) == 0;
    if (requirePrivate && inspected.kind == PathKind::Directory && !privateMode) {
        // Only the save subdirectory owned by this app is tightened.
        (void)chmod(path, 0700);
        const PathStatus retried = inspectPath(path, true);
        privateMode = retried.kind == PathKind::Directory &&
                      (retried.status.st_mode & (S_IWGRP | S_IWOTH)) == 0;
    }
    const bool ok = (mkdirResult == 0 || mkdirError == EEXIST) &&
                    inspected.kind == PathKind::Directory;
    const int count = std::snprintf(
        diagnostics.data() + used, diagnostics.size() - used,
        "%s%s: mkdir=%d/%d via=%s lstat_errno=%d open_errno=%d mode=0%o%s",
        used == 0 ? "" : "; ", path, mkdirResult, mkdirError, inspected.method,
        inspected.lstatError, inspected.openError,
        static_cast<unsigned int>(inspected.status.st_mode),
        requirePrivate && !privateMode ? " (not private)" : "");
    if (count < 0) {
        diagnostics[used] = '\0';
    }
    if (!ok) {
        return {ErrorCode::StorageFailure, {}, "PS5 save directory is unavailable; see diagnostics."};
    }
    return Status::success();
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
    const PathStatus inspected = inspectPath(path, false);
    status = inspected.status;
    return inspected.kind == PathKind::Regular;
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

constexpr std::size_t kDirectoryBufferBytes = 64 * 1024;

enum class DirectoryReader : std::uint8_t { Getdents, Getdirentries, SceGetdents, SceGetdirentries };

struct DirectoryMethod {
    const char* name;
    int openFlags;
    DirectoryReader reader;
};

constexpr int kNoFollowDirectory = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
constexpr int kPlainDirectory = O_RDONLY | O_CLOEXEC;

// Plain-open variants still verify the descriptor is a directory with fstat; the
// save root itself was already confirmed as a non-symlink directory at initialization.
constexpr DirectoryMethod kDirectoryMethods[] = {
    {"getdents64k", kNoFollowDirectory, DirectoryReader::Getdents},
    {"getdirentries64k", kNoFollowDirectory, DirectoryReader::Getdirentries},
    {"sceKernelGetdents", kNoFollowDirectory, DirectoryReader::SceGetdents},
    {"sceKernelGetdirentries", kNoFollowDirectory, DirectoryReader::SceGetdirentries},
    {"plain-getdents64k", kPlainDirectory, DirectoryReader::Getdents},
    {"plain-sceKernelGetdents", kPlainDirectory, DirectoryReader::SceGetdents},
};

// SCE kernel calls return 0x800200xx error codes whose low byte is the errno value.
[[nodiscard]] int sceResult(int result) noexcept {
    if (result < 0) {
        errno = result & 0xFF;
        return -1;
    }
    return result;
}

[[nodiscard]] int readDirectory(DirectoryReader reader, int directory, char* buffer,
                                long& base) noexcept {
    constexpr int size = static_cast<int>(kDirectoryBufferBytes);
    switch (reader) {
    case DirectoryReader::Getdents:
        return getdents(directory, buffer, size);
    case DirectoryReader::Getdirentries:
        return getdirentries(directory, buffer, size, &base);
    case DirectoryReader::SceGetdents:
        return sceResult(sceKernelGetdents(directory, buffer, size));
    case DirectoryReader::SceGetdirentries:
        return sceResult(sceKernelGetdirentries(directory, buffer, size, &base));
    }
    errno = EINVAL;
    return -1;
}

void appendAttempt(char* attempts, std::size_t& length, const char* method, const char* step,
                   int error) noexcept {
    constexpr std::size_t kCapacity = 200;
    if (length >= kCapacity - 1) {
        return;
    }
    const int added = std::snprintf(attempts + length, kCapacity - length, "%s%s:%s=%d",
                                    length != 0 ? " " : "", method, step, error);
    if (added > 0) {
        length = std::min(kCapacity - 1, length + static_cast<std::size_t>(added));
    }
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
    diagnostics_.fill('\0');
    const Status parentStatus = ensureDirectory(kParentDirectory, false, diagnostics_);
    if (!parentStatus) {
        return parentStatus;
    }
    const int count = std::snprintf(rootPath_.data(), rootPath_.size(),
                                    "%s/%.*s", kParentDirectory,
                                    static_cast<int>(subdirectory.size()), subdirectory.data());
    if (count <= 0 || static_cast<std::size_t>(count) >= rootPath_.size()) {
        rootPath_.fill('\0');
        return {ErrorCode::CapacityExceeded, {}, "PS5 save directory path exceeds its limit."};
    }
    const Status directoryStatus = ensureDirectory(rootPath_.data(), true, diagnostics_);
    if (!directoryStatus) {
        rootPath_.fill('\0');
        return directoryStatus;
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
        return fail("read: slot is missing, not regular, or wrong size");
    }
    const int descriptor = open(path, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0) {
        return fail("read: open");
    }
    if (!regularDescriptor(descriptor, pathStatus)) {
        (void)close(descriptor);
        return fail("read: descriptor identity mismatch");
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
            return fail("read: read");
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
        return fail("write: create temporary");
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
        const bool reported = fail(writeSucceeded ? "write: close" : "write: write/fsync");
        (void)unlink(temporaryPath_.data());
        temporaryPath_.fill('\0');
        return reported;
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
    const PathStatus existing = inspectPath(targetPath, false);
    if (existing.kind != PathKind::Missing && existing.kind != PathKind::Regular) {
        errno = existing.openError != 0 ? existing.openError : existing.lstatError;
        return fail("replace: target is not a regular file");
    }
    if (rename(temporaryPath_.data(), targetPath) != 0) {
        return fail("replace: rename");
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
    const PathStatus existing = inspectPath(temporaryPath_.data(), false);
    if (existing.kind == PathKind::Missing) {
        temporaryPath_.fill('\0');
        temporarySlot_.fill('\0');
        return true;
    }
    if (existing.kind != PathKind::Regular || unlink(temporaryPath_.data()) != 0) {
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
    // The application sandbox rejects opendir with EPERM and a 4 KiB getdents with
    // EINVAL (both hardware-observed), so try bounded raw-directory variants in order
    // and keep the first one the kernel accepts.
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[kDirectoryBufferBytes]);
    if (buffer == nullptr) {
        errno = ENOMEM;
        return fail("enumerate: buffer allocation");
    }
    char attempts[200]{};
    std::size_t attemptsLength = 0;
    for (const DirectoryMethod& method : kDirectoryMethods) {
        const int directory = ::open(rootPath_.data(), method.openFlags);
        if (directory < 0) {
            appendAttempt(attempts, attemptsLength, method.name, "open", errno);
            continue;
        }
        struct stat directoryStatus{};
        if (::fstat(directory, &directoryStatus) != 0 ||
            kindFromMode(directoryStatus.st_mode) != PathKind::Directory) {
            appendAttempt(attempts, attemptsLength, method.name, "fstat", errno);
            (void)::close(directory);
            continue;
        }
        long base = 0;
        errno = 0;
        int bytes = readDirectory(method.reader, directory, buffer.get(), base);
        if (bytes < 0) {
            appendAttempt(attempts, attemptsLength, method.name, "read",
                          errno != 0 ? errno : EIO);
            (void)::close(directory);
            continue;
        }
        std::size_t resultSize = 0;
        bool valid = true;
        int directoryError = 0;
        while (valid && bytes > 0) {
            valid = parseDirectoryChunk(buffer.get(), static_cast<std::size_t>(bytes),
                                        output, capacity, resultSize);
            if (!valid) {
                break;
            }
            errno = 0;
            bytes = readDirectory(method.reader, directory, buffer.get(), base);
            if (bytes < 0) {
                directoryError = errno != 0 ? errno : EIO;
            }
        }
        const bool closeSucceeded = ::close(directory) == 0;
        if (!valid) {
            return fail("enumerate: invalid or oversized slot entry");
        }
        if (directoryError != 0 || !closeSucceeded) {
            errno = directoryError;
            (void)std::snprintf(diagnostics_.data(), diagnostics_.size(),
                                "enumerate: %s failed after first chunk errno=%d%s%s",
                                method.name, directoryError,
                                attemptsLength != 0 ? "; earlier " : "", attempts);
            return false;
        }
        (void)std::snprintf(diagnostics_.data(), diagnostics_.size(),
                            "enumerate: method=%s%s%s", method.name,
                            attemptsLength != 0 ? "; earlier " : "", attempts);
        written = resultSize;
        return true;
    }
    (void)std::snprintf(diagnostics_.data(), diagnostics_.size(),
                        "enumerate: all directory methods failed: %s", attempts);
    return false;
}

bool Ps5SaveStorage::parseDirectoryChunk(const char* buffer, std::size_t bytes, char* output,
                                         std::size_t capacity,
                                         std::size_t& resultSize) noexcept {
    constexpr std::size_t kHeaderBytes = offsetof(dirent, d_name);
    std::size_t offset = 0;
    while (offset < bytes) {
        const std::size_t remaining = bytes - offset;
        if (remaining < kHeaderBytes) {
            return false;
        }
        dirent header{};
        std::memcpy(&header, buffer + offset, kHeaderBytes);
        if (header.d_reclen < kHeaderBytes || header.d_reclen > remaining ||
            header.d_namlen > header.d_reclen - kHeaderBytes) {
            return false;
        }
        const char* rawName = buffer + offset + kHeaderBytes;
        offset += header.d_reclen;
        if (header.d_fileno == 0) {
            continue;
        }
        char name[256]{};
        std::memcpy(name, rawName, header.d_namlen);
        std::array<char, 33> slot{};
        if (!copyDirectorySlot(name, slot)) {
            const std::string_view nameView(name, header.d_namlen);
            if (nameView.size() >= 4 && nameView.substr(nameView.size() - 4) == ".sav") {
                return false;
            }
            continue;
        }
        char path[160]{};
        struct stat status{};
        const std::size_t slotLength = std::strlen(slot.data());
        if (!makeSlotPath(rootPath_.data(), std::string_view(slot.data(), slotLength), ".sav",
                          path, sizeof(path)) ||
            !regularFile(path, status) || status.st_size <= 0 ||
            static_cast<unsigned long long>(status.st_size) > kMaxSaveBytes ||
            slotLength + 1 > capacity - resultSize) {
            return false;
        }
        std::memcpy(output + resultSize, slot.data(), slotLength + 1);
        resultSize += slotLength + 1;
    }
    return true;
}
bool Ps5SaveStorage::fail(const char* step) noexcept {
    const int error = errno;
    (void)std::snprintf(diagnostics_.data(), diagnostics_.size(), "%s failed errno=%d", step, error);
    return false;
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
