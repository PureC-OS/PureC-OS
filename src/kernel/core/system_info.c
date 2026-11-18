#include <arch/arch.h>
#include "system_info.h"
#include "../../drivers/interrupts/timer.h"
#include "../process/scheduler.h"
#include "../smp/cpu.h"

#include <stddef.h>

#define CPU_NAME_CAPACITY 49

static char cpu_name[CPU_NAME_CAPACITY] = "Unknown x86_64 CPU";
static uint64_t usable_ram_bytes;
static uint64_t total_ram_bytes;
static uint64_t tsc_frequency_hz = 3000000000;
static uint64_t boot_tsc;
static uint64_t cpu_sample_ticks;
static uint64_t cpu_sample_idle_ticks;

static uint64_t read_tsc(void)
{
    return arch_read_timestamp();
}

static void detect_cpu_name(void)
{
  arch_cpu_name(cpu_name, CPU_NAME_CAPACITY);
}

static void detect_tsc_frequency(void)
{
  uint64_t hz = arch_cpuid_tsc_hz();
  if (hz)
    tsc_frequency_hz = hz;
}

static void detect_usable_ram(const struct limine_memmap_response *memory_map)
{
  usable_ram_bytes = 0;
  total_ram_bytes = 0;
  if (!memory_map)
    return;

  for (uint64_t index = 0; index < memory_map->entry_count; index++)
  {
    const struct limine_memmap_entry *entry = memory_map->entries[index];
    if (entry && entry->type == LIMINE_MEMMAP_USABLE)
    {
      usable_ram_bytes += entry->length;
    }
    if (entry && (entry->type == LIMINE_MEMMAP_USABLE ||
                  entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
                  entry->type == LIMINE_MEMMAP_ACPI_NVS ||
                  entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
                  entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES ||
                  entry->type == LIMINE_MEMMAP_FRAMEBUFFER))
    {
      total_ram_bytes += entry->length;
    }
  }
}

void system_info_init(const struct limine_memmap_response *memory_map)
{
  boot_tsc = read_tsc();
  detect_cpu_name();
  detect_tsc_frequency();
  detect_usable_ram(memory_map);
  cpu_sample_ticks = 0;
  cpu_sample_idle_ticks = 0;
}

const char *system_info_cpu_name(void) { return cpu_name; }
uint64_t system_info_usable_ram_bytes(void) { return usable_ram_bytes; }
uint64_t system_info_total_ram_bytes(void) { return total_ram_bytes; }
uint64_t system_info_tsc_frequency_hz(void) { return tsc_frequency_hz; }
uint32_t system_info_logical_processors(void) { return cpu_detected_count(); }

uint64_t system_info_uptime_ms(void)
{
  if (!tsc_frequency_hz)
    return 0;
  return ((read_tsc() - boot_tsc) * 1000) / tsc_frequency_hz;
}

uint32_t system_info_cpu_usage_percent(void)
{
  uint64_t now = scheduler_total_ticks();
  uint64_t idle = scheduler_idle_ticks();
  uint64_t elapsed = now - cpu_sample_ticks;
  uint64_t idle_elapsed = idle - cpu_sample_idle_ticks;
  cpu_sample_ticks = now;
  cpu_sample_idle_ticks = idle;
  if (!elapsed)
    return 0;
  if (idle_elapsed > elapsed)
    idle_elapsed = elapsed;
  return 100U - (uint32_t)((idle_elapsed * 100) / elapsed);
}
