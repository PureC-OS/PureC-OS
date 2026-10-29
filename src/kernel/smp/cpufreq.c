#include "cpufreq.h"
#include "../diagnostics/klog.h"
#include "../sync/spinlock.h"

#define MSR_PLATFORM_INFO 0xCE
#define MSR_MPERF 0xE7
#define MSR_APERF 0xE8
#define MSR_PERF_CTL 0x199
#define MSR_MISC_ENABLE 0x1A0
#define MSR_PM_ENABLE 0x770
#define MSR_HWP_CAPABILITIES 0x771
#define MSR_HWP_REQUEST 0x774

#define MISC_ENABLE_EIST (1ULL << 16)
#define HWP_EPP_PERFORMANCE 0x00U

static enum cpufreq_mode mode;
static uint32_t tsc_mhz;

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c,
                  uint32_t *d) {
  __asm__ volatile("cpuid"
                   : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                   : "a"(leaf), "c"(0));
}
static uint64_t rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}
static void wrmsr(uint32_t msr, uint64_t value) {
  __asm__ volatile("wrmsr" ::"c"(msr), "a"((uint32_t)value),
                   "d"((uint32_t)(value >> 32))
                   : "memory");
}
static uint64_t rdtsc(void) {
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

static enum cpufreq_mode detect_mode(void) {
  uint32_t a, b, c, d, max_leaf;
  cpuid(0, &max_leaf, &b, &c, &d);
  if (b != 0x756E6547 || d != 0x49656E69 || c != 0x6C65746E)
    return CPUFREQ_NONE;
  cpuid(1, &a, &b, &c, &d);
  if (c & (1U << 31))
    return CPUFREQ_NONE;
  bool eist = (c & (1U << 7)) != 0;
  if (max_leaf >= 6) {
    cpuid(6, &a, &b, &c, &d);
    if (a & (1U << 7))
      return CPUFREQ_HWP;
  }
  return eist ? CPUFREQ_EIST : CPUFREQ_NONE;
}

static void detect_tsc_mhz(void) {
  uint32_t a, b, c, d, max_leaf;
  cpuid(0, &max_leaf, &b, &c, &d);
  if (max_leaf >= 0x15) {
    cpuid(0x15, &a, &b, &c, &d);
    if (a && b && c) {
      tsc_mhz = (uint32_t)((uint64_t)c * b / a / 1000000ULL);
      return;
    }
  }
  if (max_leaf >= 0x16) {
    cpuid(0x16, &a, &b, &c, &d);
    tsc_mhz = a;
  }
}

uint32_t cpufreq_measure_mhz(void) {
  if (mode == CPUFREQ_NONE || !tsc_mhz)
    return 0;
  uint64_t flags = irq_save();
  uint64_t tsc0 = rdtsc(), mperf0 = rdmsr(MSR_MPERF),
           aperf0 = rdmsr(MSR_APERF);
  uint64_t window = (uint64_t)tsc_mhz * 5000ULL;
  while (rdtsc() - tsc0 < window)
    __asm__ volatile("" ::: "memory");
  uint64_t mperf = rdmsr(MSR_MPERF) - mperf0;
  uint64_t aperf = rdmsr(MSR_APERF) - aperf0;
  irq_restore(flags);
  if (!mperf)
    return 0;
  return (uint32_t)((uint64_t)tsc_mhz * aperf / mperf);
}

void cpufreq_init_cpu(void) {
  if (mode == CPUFREQ_HWP) {
    wrmsr(MSR_PM_ENABLE, 1);
    uint64_t caps = rdmsr(MSR_HWP_CAPABILITIES);
    uint64_t highest = caps & 0xFF;
    uint64_t efficient = (caps >> 16) & 0xFF;
    if (!efficient || efficient > highest)
      efficient = (caps >> 24) & 0xFF;
    wrmsr(MSR_HWP_REQUEST, efficient | (highest << 8) |
                               ((uint64_t)HWP_EPP_PERFORMANCE << 24));
  } else if (mode == CPUFREQ_EIST) {
    wrmsr(MSR_MISC_ENABLE, rdmsr(MSR_MISC_ENABLE) | MISC_ENABLE_EIST);
    uint64_t max_ratio = (rdmsr(MSR_PLATFORM_INFO) >> 8) & 0xFF;
    if (max_ratio)
      wrmsr(MSR_PERF_CTL, max_ratio << 8);
  }
}

void cpufreq_init_bsp(void) {
  mode = detect_mode();
  detect_tsc_mhz();
  if (mode == CPUFREQ_NONE) {
    klog(KLOG_INFO, "cpufreq: no Intel HWP/EIST control (VM or non-Intel)");
    return;
  }
  uint32_t before = cpufreq_measure_mhz();
  cpufreq_init_cpu();
  uint32_t after = cpufreq_measure_mhz();
  if (mode == CPUFREQ_HWP) {
    uint64_t caps = rdmsr(MSR_HWP_CAPABILITIES);
    klogf(KLOG_OK,
          "cpufreq: HWP enabled ratios highest=%u guaranteed=%u efficient=%u "
          "lowest=%u; cpu0 %u MHz -> %u MHz (TSC %u MHz)",
          (uint32_t)(caps & 0xFF), (uint32_t)((caps >> 8) & 0xFF),
          (uint32_t)((caps >> 16) & 0xFF), (uint32_t)((caps >> 24) & 0xFF),
          before, after, tsc_mhz);
  } else {
    klogf(KLOG_OK, "cpufreq: EIST max ratio; cpu0 %u MHz -> %u MHz (TSC %u MHz)",
          before, after, tsc_mhz);
  }
}

enum cpufreq_mode cpufreq_get_mode(void) { return mode; }
