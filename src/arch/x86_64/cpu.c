#include <arch/arch.h>
#include <stddef.h>
#include <lib/string.h>

static void copy_register(char *destination, uint32_t value)
{
    for (uint32_t byte = 0; byte < 4; byte++)
        destination[byte] = (char)(value >> (byte * 8));
}

void arch_cpu_name(char *name, size_t capacity)
{
    uint32_t eax, ebx, ecx, edx;
    arch_cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);

    if (eax >= 0x80000004) {
        for (uint32_t part = 0; part < 3; part++) {
            arch_cpuid(0x80000002 + part, 0, &eax, &ebx, &ecx, &edx);
            char *destination = &name[part * 16];
            copy_register(destination, eax);
            copy_register(destination + 4, ebx);
            copy_register(destination + 8, ecx);
            copy_register(destination + 12, edx);
        }
        name[capacity - 1] = '\0';
        size_t start = 0;
        while (name[start] == ' ')
            start++;
        if (start == 0)
            return;
        size_t destination = 0;
        do {
            name[destination++] = name[start++];
        } while (name[start - 1]);
        return;
    }

    arch_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    copy_register(name, ebx);
    copy_register(name + 4, edx);
    copy_register(name + 8, ecx);
    name[12] = '\0';
}

void arch_cpu_brand_string(char brand[49])
{
    uint32_t maximum, ebx, ecx, edx;
    arch_cpuid(0x80000000u, 0, &maximum, &ebx, &ecx, &edx);
    brand[0] = '\0';
    if (maximum < 0x80000004u)
        return;
    uint32_t words[12];
    for (uint32_t leaf = 0; leaf < 3; leaf++)
        arch_cpuid(0x80000002u + leaf, 0, &words[leaf * 4], &words[leaf * 4 + 1],
                   &words[leaf * 4 + 2], &words[leaf * 4 + 3]);
    memcpy(brand, words, 48);
    brand[48] = '\0';
    char *start = brand;
    while (*start == ' ')
        start++;
    if (start != brand)
        memmove(brand, start, strlen(start) + 1);
}

uint64_t arch_cpuid_tsc_hz(void)
{
    uint32_t maximum, eax, ebx, ecx, edx;
    arch_cpuid(0, 0, &maximum, &ebx, &ecx, &edx);
    if (maximum >= 0x15) {
        arch_cpuid(0x15, 0, &eax, &ebx, &ecx, &edx);
        if (eax && ebx && ecx)
            return ((uint64_t)ecx * ebx) / eax;
    }
    if (maximum >= 0x16) {
        arch_cpuid(0x16, 0, &eax, &ebx, &ecx, &edx);
        if (eax)
            return (uint64_t)eax * 1000000ULL;
    }
    return 0;
}

bool arch_cpu_has_nx(void)
{
    uint32_t maximum, edx;
    arch_cpuid(0x80000000U, 0, &maximum, NULL, NULL, NULL);
    if (maximum < 0x80000001U)
        return false;
    arch_cpuid(0x80000001U, 0, NULL, NULL, NULL, &edx);
    return (edx & (1U << 20)) != 0;
}
