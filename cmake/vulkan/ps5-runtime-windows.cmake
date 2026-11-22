include("${CMAKE_CURRENT_LIST_DIR}/../ps5-windows.cmake")
set(CMAKE_ASM_COMPILER "${LLVM_ROOT}/bin/clang.exe")
set(CMAKE_ASM_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_ASM_FLAGS_INIT "--target=x86_64-sie-ps5 -fPIC")
