#pragma once
// PureC hosted libc: ctype, phase 1 of the TCC migration foundation.
// Classification is pure ASCII-range checks, no locale tables needed.

int isalpha(int c);
int isupper(int c);
int islower(int c);
int isdigit(int c);
int isxdigit(int c);
int isalnum(int c);
int isspace(int c);
int ispunct(int c);
int isprint(int c);
int isgraph(int c);
int iscntrl(int c);
int isblank(int c);
int tolower(int c);
int toupper(int c);
int isascii(int c);
int toascii(int c);
