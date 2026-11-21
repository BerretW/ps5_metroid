#pragma once

#include "echo/framework/data.hpp"

#include <cstdio>
#include <cstdlib>

namespace echo::framework::tools {

[[nodiscard]] inline Status readJsonFile(const char* path, JsonDocument& document,
                                         JsonError& error,
                                         std::size_t maximumBytes = kMaxJsonBytes) noexcept {
    if (path == nullptr) {
        return {ErrorCode::InvalidArgument, {}, "JSON input path is required."};
    }
    if (maximumBytes == 0 || maximumBytes > kMaxJsonBytes) {
        return {ErrorCode::InvalidArgument, path,
                "JSON input limit must be within the framework byte limit."};
    }

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return {ErrorCode::StorageFailure, path, "Could not open JSON input."};
    }
    char* input = static_cast<char*>(std::malloc(maximumBytes + 1));
    if (input == nullptr) {
        std::fclose(file);
        return {ErrorCode::StorageFailure, path, "Could not allocate JSON input buffer."};
    }

    const std::size_t size = std::fread(input, 1, maximumBytes + 1, file);
    const bool readFailed = std::ferror(file) != 0;
    const int closeResult = std::fclose(file);
    if (readFailed || closeResult != 0) {
        std::free(input);
        return {ErrorCode::StorageFailure, path, "Could not read complete JSON input."};
    }
    if (size == 0) {
        std::free(input);
        return {ErrorCode::InvalidValue, path, "JSON input is empty."};
    }
    if (size > maximumBytes) {
        std::free(input);
        return {ErrorCode::CapacityExceeded, path,
                "JSON input exceeds its configured byte limit."};
    }

    const Status parsed = JsonDocument::parse(input, size, document, error);
    std::free(input);
    if (!parsed) {
        return {ErrorCode::ParseError, path, error.message};
    }
    return Status::success();
}

inline void reportStatus(const Status& status) {
    std::fprintf(stderr, "Operation failed (code %u)",
                 static_cast<unsigned int>(status.code));
    if (!status.subject.empty()) {
        std::fprintf(stderr, " at %.*s", static_cast<int>(status.subject.size()),
                     status.subject.data());
    }
    if (!status.detail.empty()) {
        std::fprintf(stderr, ": %.*s", static_cast<int>(status.detail.size()),
                     status.detail.data());
    }
    std::fputc('\n', stderr);
}

} // namespace echo::framework::tools
