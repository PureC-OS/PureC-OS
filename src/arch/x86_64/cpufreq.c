#include <arch/arch.h>
#include <arch/x86_64/cpufreq.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/sync/spinlock.h>

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
static uint64_t rdtsc(void)
{
    return arch_read_timestamp();
}

static enum cpufreq_mode detect_mode(void) {
  uint32_t a, b, c, d, max_leaf;
  arch_cpuid(0, 0, &max_leaf, &b, &c, &d);
  if (b != 0x756E6547 || d != 0x49656E69 || c != 0x6C65746E)
    return CPUFREQ_NONE;
  arch_cpuid(1, 0, &a, &b, &c, &d);
  if (c & (1U << 31))
    return CPUFREQ_NONE;
  bool eist = (c & (1U << 7)) != 0;
  if (max_leaf >= 6) {
    arch_cpuid(6, 0, &a, &b, &c, &d);
    if (a & (1U << 7))
      return CPUFREQ_HWP;
  }
  return eist ? CPUFREQ_EIST : CPUFREQ_NONE;
}

static void detect_tsc_mhz(void) {
  uint32_t a, b, c, d, max_leaf;
  arch_cpuid(0, 0, &max_leaf, &b, &c, &d);
  if (max_leaf >= 0x15) {
    arch_cpuid(0x15, 0, &a, &b, &c, &d);
    if (a && b && c) {
      tsc_mhz = (uint32_t)((uint64_t)c * b / a / 1000000ULL);
      return;
    }
  }
  if (max_leaf >= 0x16) {
    arch_cpuid(0x16, 0, &a, &b, &c, &d);
    tsc_mhz = a;
  }
}

uint32_t cpufreq_measure_mhz(void) {
  if (mode == CPUFREQ_NONE || !tsc_mhz)
    return 0;
  uint64_t flags = irq_save();
  uint64_t tsc0 = rdtsc(), mperf0 = arch_rdmsr(MSR_MPERF),
           aperf0 = arch_rdmsr(MSR_APERF);
  uint64_t window = (uint64_t)tsc_mhz * 5000ULL;
  while (rdtsc() - tsc0 < window)
    __asm__ volatile("" ::: "memory");
  uint64_t mperf = arch_rdmsr(MSR_MPERF) - mperf0;
  uint64_t aperf = arch_rdmsr(MSR_APERF) - aperf0;
  irq_restore(flags);
  if (!mperf)
    return 0;
  return (uint32_t)((uint64_t)tsc_mhz * aperf / mperf);
}

void cpufreq_init_cpu(void) {
  if (mode == CPUFREQ_HWP) {
    arch_wrmsr(MSR_PM_ENABLE, 1);
    uint64_t caps = arch_rdmsr(MSR_HWP_CAPABILITIES);
    uint64_t highest = caps & 0xFF;
    uint64_t efficient = (caps >> 16) & 0xFF;
    if (!efficient || efficient > highest)
      efficient = (caps >> 24) & 0xFF;
    arch_wrmsr(MSR_HWP_REQUEST, efficient | (highest << 8) |
                               ((uint64_t)HWP_EPP_PERFORMANCE << 24));
  } else if (mode == CPUFREQ_EIST) {
    arch_wrmsr(MSR_MISC_ENABLE, arch_rdmsr(MSR_MISC_ENABLE) | MISC_ENABLE_EIST);
    uint64_t max_ratio = (arch_rdmsr(MSR_PLATFORM_INFO) >> 8) & 0xFF;
    if (max_ratio)
      arch_wrmsr(MSR_PERF_CTL, max_ratio << 8);
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
    uint64_t caps = arch_rdmsr(MSR_HWP_CAPABILITIES);
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

uint32_t arch_cpu_freq_mhz(void) {
  return cpufreq_measure_mhz();
}
