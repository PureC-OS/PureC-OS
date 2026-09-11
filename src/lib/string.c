#include "string.h"

size_t strlen(const char* s){ size_t n=0; while(s[n]) n++; return n; }
int strcmp(const char* a, const char* b){ while(*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char* a, const char* b, size_t n){ for(size_t i=0;i<n;i++){ if(a[i]!=b[i]) return (unsigned char)a[i]-(unsigned char)b[i]; if(a[i]==0) return 0; } return 0; }
void* memcpy(void* dst, const void* src, size_t n){ uint8_t *d=dst; const uint8_t *s=src; for(size_t i=0;i<n;i++) d[i]=s[i]; return dst; }
void* memmove(void* dst, const void* src, size_t n){ uint8_t *d=dst; const uint8_t *s=src; if(d<s){ for(size_t i=0;i<n;i++) d[i]=s[i]; } else if(d>s){ for(size_t i=n;i>0;i--) d[i-1]=s[i-1]; } return dst; }
void* memset(void* s, int c, size_t n){ uint8_t *p=s; for(size_t i=0;i<n;i++) p[i]=c; return s; }
int memcmp(const void* a, const void* b, size_t n){ const uint8_t *p=a,*q=b; for(size_t i=0;i<n;i++) if(p[i]!=q[i]) return p[i]-q[i]; return 0; }
char* strcpy(char* dst, const char* src){ char *d=dst; while((*d++=*src++)); return dst; }
char* strncpy(char* dst, const char* src, size_t n){ size_t i=0; for(;i<n && src[i];i++) dst[i]=src[i]; for(;i<n;i++) dst[i]=0; return dst; }
char* strcat(char* dst, const char* src){ char *d=dst; while(*d) d++; while((*d++=*src++)); return dst; }
