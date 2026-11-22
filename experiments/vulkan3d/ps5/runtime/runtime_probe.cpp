#include <ps5platform/heap.h>
#include <ps5platform/klog.h>
#include <ps5platform/libc.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <locale>
#include <new>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <utility>

extern "C" {
int sceKernelOpen(const char* path, int flags, unsigned int mode);
std::int64_t sceKernelWrite(int descriptor, const void* buffer, std::size_t size);
int sceKernelFsync(int descriptor);
int sceKernelClose(int descriptor);
#ifdef ECHO_PS5_RUNTIME_HOLD
int sceKernelUsleep(std::uint32_t microseconds);
#endif
int __real_atexit(void (*callback)());
int __real___cxa_atexit(void (*callback)(void*), void* argument, void* dso);
[[noreturn]] void __real_exit(int status);
void _fini();
}

extern "C" {
__attribute__((visibility("hidden"))) void* __dso_handle = &__dso_handle;
}

namespace {
void lifecycleEvent(const char* stage, int code) noexcept {
    char line[160]{};
    const int size = std::snprintf(line, sizeof(line),
        "{\"stage\":\"%s\",\"code\":%d}\n", stage, code);
    if (size < 0 || static_cast<std::size_t>(size) >= sizeof(line)) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: lifecycle event formatting failed");
        return;
    }
    (void)sceKernelDebugOutText(0, line);
    const int descriptor = sceKernelOpen("/data/echo-vulkan-lab/startup.log",
        O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (descriptor < 0) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: lifecycle log open failed");
        return;
    }
    std::size_t written = 0;
    while (written < static_cast<std::size_t>(size)) {
        const auto count = sceKernelWrite(descriptor, line + written,
            static_cast<std::size_t>(size) - written);
        if (count <= 0 || static_cast<std::uint64_t>(count) >
            static_cast<std::size_t>(size) - written) {
            (void)sceKernelDebugOutText(0, "ECHO runtime probe: lifecycle log write failed");
            break;
        }
        written += static_cast<std::size_t>(count);
    }
    if (sceKernelFsync(descriptor) != 0) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: lifecycle log sync failed");
    }
    if (sceKernelClose(descriptor) != 0) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: lifecycle log close failed");
    }
}

struct ExitCallback {
    void (*function)(){};
};

std::array<ExitCallback, 32> exitCallbacks{};
struct CxaCallback {
    void (*function)(void*);
    void* argument;
};
std::array<CxaCallback, 128> cxaCallbacks{};
unsigned cxaCallbackCount{};
int sentinelResult = -1;
bool sentinelAttempted{};

void exitSentinel() {
    lifecycleEvent("crt.exit_sentinel", 0);
}

template<std::size_t Index>
void invokeExitCallback() {
    const auto callback = exitCallbacks[Index].function;
    if (callback == nullptr) return;
    const bool fini = callback == &_fini;
    lifecycleEvent(fini ? "crt.fini.begin" : "crt.exit_callback.begin",
        static_cast<int>(Index));
    callback();
    lifecycleEvent(fini ? "crt.fini.end" : "crt.exit_callback.end",
        static_cast<int>(Index));
}

template<std::size_t... Indices>
constexpr auto makeExitTrampolines(std::index_sequence<Indices...>) {
    return std::array<void (*)(), sizeof...(Indices)>{&invokeExitCallback<Indices>...};
}

constexpr auto exitTrampolines = makeExitTrampolines(std::make_index_sequence<32>{});

template<std::size_t Index>
void invokeCxaCallback(void* opaque) {
    auto& entry = *static_cast<CxaCallback*>(opaque);
    lifecycleEvent("crt.cxa_callback.begin", static_cast<int>(Index));
    entry.function(entry.argument);
    lifecycleEvent("crt.cxa_callback.end", static_cast<int>(Index));
}

template<std::size_t... Indices>
constexpr auto makeCxaTrampolines(std::index_sequence<Indices...>) {
    return std::array<void (*)(void*), sizeof...(Indices)>{&invokeCxaCallback<Indices>...};
}

constexpr auto cxaTrampolines = makeCxaTrampolines(std::make_index_sequence<128>{});

thread_local unsigned tlsToken = 0x1357;
struct Worker {
    unsigned assigned{};
    bool isolated{};
    bool directTls{};
};

void* worker(void* argument) noexcept {
    auto& state = *static_cast<Worker*>(argument);
    state.isolated = tlsToken == 0x1357;
    tlsToken = state.assigned;
    state.directTls = ps5_heap_owns(&tlsToken);
    state.isolated = state.isolated && tlsToken == state.assigned;
    return nullptr;
}

bool event(FILE* log, const char* stage, bool success, int code = 0) noexcept {
    char line[256]{};
    const int size = std::snprintf(line, sizeof(line),
        "{\"stage\":\"%s\",\"result\":\"%s\",\"code\":%d}\n", stage, success ? "ok" : "error", code);
    if (size < 0 || static_cast<std::size_t>(size) >= sizeof(line)) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: event formatting failed");
        return false;
    }
    (void)sceKernelDebugOutText(0, line);
    if (std::fputs(line, log) == EOF || std::fflush(log) != 0) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: log write failed");
        return false;
    }
    return success;
}

bool run(FILE* log) {
    if (!event(log, "runtime.start", true) || !event(log, "vulkan.not_initialized", true)) return false;
    constexpr std::size_t bytes = 8U * 1024U * 1024U;
    void* allocation = ps5_heap_malloc(bytes);
    const bool direct = allocation && ps5_heap_owns(allocation);
    if (direct) {
        auto* data = static_cast<volatile unsigned char*>(allocation);
        for (std::size_t i = 0; i < bytes; i += 4096) data[i] = static_cast<unsigned char>(i / 4096);
    }
    ps5_heap_free(allocation);
    if (!event(log, "heap.direct_8mib", direct)) return false;

    auto* owned = new (std::nothrow) unsigned char[256U * 1024U];
    const bool bridged = owned && ps5_heap_owns(owned);
    delete[] owned;
    if (!event(log, "cpp.allocator", bridged)) return false;

    const std::string text(64, 'E');
    const auto& facet = std::use_facet<std::ctype<char>>(std::locale::classic());
    if (!event(log, "cpp.library_locale", ps5_heap_owns(text.data()) &&
        facet.is(std::ctype_base::alpha, text[0]))) return false;
    if (!event(log, "tls.main", tlsToken == 0x1357 && ps5_heap_owns(&tlsToken))) return false;
    tlsToken = 0x2468;

    unsigned baseline{}, cached{};
    ps5_thread_stacks(&baseline, &cached);
    Worker workers[2]{{0x1111}, {0x2222}};
    pthread_t threads[2]{};
    unsigned created{};
    int createError{};
    for (; created < 2; ++created) {
        createError = pthread_create(&threads[created], nullptr, worker, &workers[created]);
        if (createError != 0) break;
    }
    unsigned active{};
    ps5_thread_stacks(&active, &cached);
    bool joined = true;
    int joinError{};
    for (unsigned i = 0; i < created; ++i) {
        const int result = pthread_join(threads[i], nullptr);
        if (result != 0) { joined = false; joinError = result; }
    }
    if (!event(log, "threads.create", created == 2, createError) ||
        !event(log, "threads.join", joined, joinError) ||
        !event(log, "threads.direct_stacks", active == baseline + 2) ||
        !event(log, "tls.isolated", workers[0].isolated && workers[1].isolated &&
            workers[0].directTls && workers[1].directTls && tlsToken == 0x2468)) return false;
    ps5_thread_stacks(&active, &cached);
    if (!event(log, "threads.released", active == baseline)) return false;

    struct ps5_heap_stats stats{};
    ps5_heap_stats(&stats);
    if (std::fprintf(log, "{\"stage\":\"heap.stats\",\"mappedBytes\":%llu,\"peakBytes\":%llu,"
        "\"arenas\":%u,\"libcFallbacks\":%llu,\"cachedStacks\":%u}\n",
        static_cast<unsigned long long>(stats.mapped_bytes), static_cast<unsigned long long>(stats.peak_bytes),
        stats.arenas, stats.libc_fallbacks, cached) < 0) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: heap statistics log write failed");
        return false;
    }
    if (!event(log, "heap.no_libc_fallback", stats.range_base != 0 && stats.libc_fallbacks == 0)) return false;
    return event(log, "runtime.complete", true);
}
}

extern "C" __attribute__((visibility("hidden"))) void catchReturnFromMain(int status) {
    lifecycleEvent("crt.main_return", status);
}

extern "C" __attribute__((visibility("hidden"))) int __wrap_atexit(void (*callback)()) {
    if (!sentinelAttempted) {
        sentinelAttempted = true;
        sentinelResult = __real_atexit(exitSentinel);
        if (sentinelResult != 0) {
            lifecycleEvent("crt.exit_sentinel.registration_failed", sentinelResult);
        }
    }
    for (std::size_t index = 0; index < exitCallbacks.size(); ++index) {
        if (exitCallbacks[index].function == nullptr) {
            exitCallbacks[index].function = callback;
            lifecycleEvent("crt.exit_callback.registered", static_cast<int>(index));
            const int result = __real_atexit(exitTrampolines[index]);
            if (result != 0) {
                exitCallbacks[index].function = nullptr;
                lifecycleEvent("crt.exit_callback.registration_failed", result);
            }
            return result;
        }
    }
    lifecycleEvent("crt.exit_callback.capacity_exceeded", 32);
    return __real_atexit(callback);
}

extern "C" __attribute__((visibility("hidden"))) int __wrap___cxa_atexit(
    void (*callback)(void*), void* argument, void* dso) {
    const unsigned index = __atomic_fetch_add(&cxaCallbackCount, 1, __ATOMIC_RELAXED);
    if (index >= cxaCallbacks.size()) {
        lifecycleEvent("crt.cxa_callback.capacity_exceeded", static_cast<int>(index));
        return __real___cxa_atexit(callback, argument, dso);
    }
    cxaCallbacks[index] = {callback, argument};
    const int result = __real___cxa_atexit(cxaTrampolines[index], &cxaCallbacks[index], dso);
    if (result != 0) lifecycleEvent("crt.cxa_callback.registration_failed", result);
    return result;
}

extern "C" [[noreturn]] __attribute__((visibility("hidden"))) void __wrap_exit(int status) {
    lifecycleEvent("crt.exit_called", status);
    __real_exit(status);
}

int main() {
    if (mkdir("/data/echo-vulkan-lab", 0777) != 0 && errno != EEXIST) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: log directory creation failed");
        return 1;
    }
    FILE* log = std::fopen("/data/echo-vulkan-lab/startup.log", "wb");
    if (!log) {
        (void)sceKernelDebugOutText(0, "ECHO runtime probe: startup log open failed");
        return 1;
    }
    const bool success = run(log);
    const int flushResult = std::fflush(log);
    lifecycleEvent("main.flush", flushResult);
    lifecycleEvent("crt.exit_sentinel.registration", sentinelResult);
    lifecycleEvent("crt.cxa_callback.registrations", static_cast<int>(
        __atomic_load_n(&cxaCallbackCount, __ATOMIC_RELAXED)));
    lifecycleEvent("main.before_fclose", success ? 0 : 1);
    const int closeResult = std::fclose(log);
    if (closeResult != 0) (void)sceKernelDebugOutText(0, "ECHO runtime probe: startup log close failed");
    lifecycleEvent("main.after_fclose", closeResult);
#ifdef ECHO_PS5_RUNTIME_HOLD
    if (success && flushResult == 0 && closeResult == 0) {
        lifecycleEvent("diagnostic.hold.start", 0);
        for (int heartbeat = 1;; ++heartbeat) {
            const int sleepResult = sceKernelUsleep(5000000);
            if (sleepResult != 0) {
                lifecycleEvent("diagnostic.hold.sleep_failed", sleepResult);
                return 1;
            }
            lifecycleEvent("diagnostic.hold.heartbeat", heartbeat);
            if (heartbeat == 12) heartbeat = 0;
        }
    }
#endif
    return success && flushResult == 0 && closeResult == 0 ? 0 : 1;
}
