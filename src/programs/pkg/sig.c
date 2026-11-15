#include "pkgd.h"

int sig_verify_file(const char *file, const char *signature_file)
{
    (void)file;
    (void)signature_file;
    return SIG_NOKEYS;
}
