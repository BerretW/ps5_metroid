cmake_minimum_required(VERSION 3.24)
project(echo_ps5_runtime_probe LANGUAGES C CXX)
option(ECHO_PS5_RUNTIME_HOLD "Keep the runtime diagnostic open after successful checks" OFF)
if(NOT PS5 OR NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "Use the experimental native Windows PS5 toolchain.")
endif()
file(TO_CMAKE_PATH "${BOILERPLATE_SOURCE}" BOILERPLATE_SOURCE)
set(native "${BOILERPLATE_SOURCE}/tooling/native")
add_executable(echo_vulkan_runtime runtime_probe.cpp "${native}/app_crt.cpp" "${native}/app_cpp_runtime.cpp")
set_target_properties(echo_vulkan_runtime PROPERTIES SUFFIX ".llvm.elf" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
set_property(TARGET echo_vulkan_runtime APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime-layout.ld" "${CMAKE_CURRENT_SOURCE_DIR}/runtime-symbols.map"
    "${native}/ps5-pie.ld")
target_compile_options(echo_vulkan_runtime PRIVATE -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
if(ECHO_PS5_RUNTIME_HOLD)
    target_compile_definitions(echo_vulkan_runtime PRIVATE ECHO_PS5_RUNTIME_HOLD=1)
endif()
target_link_directories(echo_vulkan_runtime PRIVATE "${PS5_SDK_ROOT}/target/lib")
target_link_options(echo_vulkan_runtime PRIVATE
    "-T${CMAKE_CURRENT_SOURCE_DIR}/runtime-layout.ld" "-L${native}"
    "--version-script=${CMAKE_CURRENT_SOURCE_DIR}/runtime-symbols.map" -e _start --gc-sections
    --wrap=atexit --wrap=__cxa_atexit --wrap=exit)
foreach(symbol malloc calloc realloc free posix_memalign aligned_alloc memalign malloc_usable_size
        reallocf reallocarray getline getdelim pthread_create pthread_join pthread_detach fclose fflush)
    target_link_options(echo_vulkan_runtime PRIVATE "--wrap=${symbol}")
endforeach()
foreach(symbol newlocale freelocale strtod_l strtof_l localeconv_l strtoll_l strtoull_l strtold_l
        snprintf_l sscanf_l asprintf_l strcoll_l strxfrm_l strftime_l wcscoll_l wcsxfrm_l
        btowc_l wctob_l iswctype_l mbrlen_l mbrtowc_l mbsrtowcs_l mbsnrtowcs_l wcrtomb_l
        wcsnrtombs_l mbtowc_l ___mb_cur_max_l ___runetype_l ___tolower_l ___toupper_l
        __runes_for_locale catopen catgets catclose __cxa_thread_atexit_impl __assert)
    target_link_options(echo_vulkan_runtime PRIVATE "--defsym=${symbol}=ps5_${symbol}")
endforeach()
target_link_libraries(echo_vulkan_runtime PRIVATE --start-group
    "${PS5_SDK_ROOT}/target/lib/libc++.a"
    "${PS5_SDK_ROOT}/target/lib/libc++abi.a"
    "${PS5_SDK_ROOT}/target/lib/libunwind.a"
    "${PS5_SDK_ROOT}/target/lib/libclang_rt.builtins-x86_64.a"
    "${PS5_SDK_ROOT}/target/lib/libps5platform.a"
    --end-group --as-needed
    "${PS5_SDK_ROOT}/target/lib/libkernel.so"
    "${PS5_SDK_ROOT}/target/lib/libSceLibcInternal.so")
