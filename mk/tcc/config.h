#pragma once
// Hand-rolled TCC configuration for PureC OS (replaces configure output).
// Disk layout this config assumes (see installer payload + ISO build):
//   /lib/tcc/include  - TCC's bundled headers (float.h, stdarg.h, ...)
//   /include           - PureC hosted libc headers (stdio.h, ...)
//   /lib               - libpurec.a, crt0.o
// Linking model: -nostdlib + explicit /lib/crt0.o + -L/lib -lpurec
// (no crt1/crti/crtn on PureC; see CONFIG notes below).

#define TCC_VERSION "0.9.28rc"

// Single-threaded: no semaphore backend (no pthreads yet).
#define CONFIG_TCC_SEMLOCK 0

// {B} substitution base for the paths below.
#define CONFIG_TCCDIR "/lib/tcc"

// Bundled headers first, then the hosted libc headers.
#define CONFIG_TCC_SYSINCLUDEPATHS "/lib/tcc/include:/include"

// -l search path for libpurec.a etc.
#define CONFIG_TCC_LIBPATHS "/lib"

// crt0.o search path for the PureC stanza in tccelf_add_crtbegin.
#define CONFIG_TCC_CRTPREFIX "/lib"
