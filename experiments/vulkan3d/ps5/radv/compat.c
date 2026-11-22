#include <errno.h>
#include <stddef.h>
#include <stdint.h>

int ps5_mkstemps(char *path_template, int suffix_length);

int echo_radv_mkstemp(char *path_template)
{
    return ps5_mkstemps(path_template, 0);
}

intptr_t echo_radv_readlink(const char *path, char *buffer, size_t size)
{
    /* The only current caller is Mesa's optional /proc GPU-info diagnostic. */
    (void)path;
    (void)buffer;
    (void)size;
    errno = ENOSYS;
    return -1;
}
