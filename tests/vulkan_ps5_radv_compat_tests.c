#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int echo_radv_mkstemp(char *path_template);
intptr_t echo_radv_readlink(const char *path, char *buffer, size_t size);

static int observed_suffix = -1;
static char *observed_template;

int ps5_mkstemps(char *path_template, int suffix_length)
{
    observed_template = path_template;
    observed_suffix = suffix_length;
    errno = EACCES;
    return -1;
}

int main(void)
{
    char name[] = "exampleXXXXXX";
    errno = 0;
    if (echo_radv_mkstemp(name) != -1 || errno != EACCES ||
        observed_template != name || observed_suffix != 0) {
        fputs("mkstemp did not preserve the provider's error and zero suffix\n", stderr);
        return 1;
    }

    char buffer[] = "unchanged";
    errno = 0;
    if (echo_radv_readlink("/proc/self/fd/1", buffer, sizeof(buffer)) != -1 ||
        errno != ENOSYS || strcmp(buffer, "unchanged") != 0) {
        fputs("unsupported readlink did not fail explicitly\n", stderr);
        return 1;
    }
    return 0;
}
