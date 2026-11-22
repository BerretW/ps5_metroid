#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *linker = _wgetenv(L"ECHO_PS5_LLD");
    if (linker == NULL || *linker == L'\0') {
        fputs("ECHO_PS5_LLD must name the native ld.lld.exe\n", stderr);
        return 127;
    }

    const wchar_t **arguments = calloc((size_t)argc + 1, sizeof(*arguments));
    if (arguments == NULL) {
        fputs("Unable to allocate linker arguments\n", stderr);
        return 127;
    }
    arguments[0] = linker;
    for (int i = 1; i < argc; ++i) {
        arguments[i] = argv[i];
    }
    intptr_t result = _wspawnv(_P_WAIT, linker, arguments);
    free(arguments);
    if (result == -1) {
        perror("Unable to start native ld.lld.exe");
        return 127;
    }
    return (int)result;
}
