// Power operations frontend.
//
// ACPI lives in a dynamically loaded kernel module (acpi.elf). This
// unit resolves the module's symbols after kmod loads it and calls
// into them. When the module is missing (fallback boot entry), legacy
// paths (KBC/CF9/triple-fault/QEMU ports/halt) keep reboot/shutdown
// working in degraded mode.
#include "power.h"
#include "../../boot/limine.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/module/kmod.h"
#include "../../lib/string.h"
#include <stddef.h>

extern struct limine_rsdp_response *rsdp_response_ptr;
extern uint64_t hhdm_offset_global;

#define ACPI_MODULE "acpi"
#define ACPI_LIMINE_PATH "/bin/modules/acpi.elf"

static int (*p_acpi_init)(void *rsdp, uint64_t hhdm) = NULL;
static void (*p_acpi_dump)(void) = NULL;
static void (*p_acpi_shutdown)(void) = NULL;
static void (*p_acpi_reboot)(void) = NULL;
static bool (*p_acpi_ready)(void) = NULL;
static bool (*p_acpi_has_battery)(void) = NULL;
static bool acpi_available = false;

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0,%1" ::"a"(v), "Nd"(port));
}

static void delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 100000u; i++)
        __asm__ volatile("pause");
}

static void legacy_halt(void) {
    for (;;)
        __asm__ volatile("cli; hlt");
}

static void legacy_reboot_fallback(void) {
    // Keyboard controller reset pulse.
    for (int i = 0; i < 10; i++) {
        if (!(inb(0x64) & 0x02)) {
            outb(0x64, 0xFE);
            delay_ms(100);
        }
    }
    // PCI reset via CF9.
    outb(0xCF9, 0x02);
    delay_ms(100);
    outb(0xCF9, 0x06);
    delay_ms(100);
    // Triple fault.
    klog(KLOG_ERROR, "power: reboot fallbacks failed, triple fault");
    __asm__ volatile("cli");
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) bad_idt = {0, 0};
    __asm__ volatile("lidt %0" ::"m"(bad_idt));
    __asm__ volatile("int $0" ::: "memory");
    legacy_halt();
}

static void legacy_shutdown_fallback(void) {
    // QEMU/KVM compat: q35 (0x604) and bochs (0xB004) power-off ports.
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outb(0xB2, 0x0F);
    delay_ms(200);
    klog(KLOG_ERROR, "power: shutdown failed on this hardware, halting CPU");
    legacy_halt();
}

void power_init(void) {
    if (kmod_load_limine(ACPI_LIMINE_PATH) != 0) {
        klog(KLOG_WARN, "power: ACPI module not loaded, using legacy fallbacks");
        return;
    }
    p_acpi_init = kmod_get_symbol(ACPI_MODULE, "acpi_init");
    p_acpi_dump = kmod_get_symbol(ACPI_MODULE, "acpi_dump_tables");
    p_acpi_shutdown = kmod_get_symbol(ACPI_MODULE, "acpi_shutdown");
    p_acpi_reboot = kmod_get_symbol(ACPI_MODULE, "acpi_reboot");
    p_acpi_ready = kmod_get_symbol(ACPI_MODULE, "acpi_is_ready");
    p_acpi_has_battery = kmod_get_symbol(ACPI_MODULE, "acpi_has_battery");
    if (!p_acpi_init || !p_acpi_shutdown || !p_acpi_reboot) {
        klog(KLOG_ERROR, "power: ACPI module misses required symbols, using legacy fallbacks");
        p_acpi_init = NULL;
        p_acpi_shutdown = NULL;
        p_acpi_reboot = NULL;
        return;
    }
    void *rsdp = (rsdp_response_ptr && rsdp_response_ptr->address)
                     ? rsdp_response_ptr->address
                     : NULL;
    if (p_acpi_init(rsdp, hhdm_offset_global) != 0) {
        klog(KLOG_WARN, "power: ACPI init failed, using legacy fallbacks");
        p_acpi_shutdown = NULL;
        p_acpi_reboot = NULL;
        return;
    }
    acpi_available = true;
    if (p_acpi_dump)
        p_acpi_dump();
    kmod_list();
}

void power_reboot(void) {
    if (p_acpi_reboot) {
        p_acpi_reboot(); // normally never returns
        klog(KLOG_ERROR, "power: ACPI reboot returned unexpectedly");
    }
    legacy_reboot_fallback();
}

void power_shutdown(void) {
    if (p_acpi_shutdown) {
        p_acpi_shutdown(); // normally never returns
        klog(KLOG_ERROR, "power: ACPI shutdown returned unexpectedly");
    }
    legacy_shutdown_fallback();
}

// Battery reporting (stub levels, real presence via ACPI module probe).
bool power_battery_get(struct battery_info *out) {
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    static uint32_t call_count = 0;
    call_count++;
    uint32_t percent = 75 + (call_count % 26); // 75-100
    if (percent > 100)
        percent = 100;
    bool charging = (call_count % 4) < 3; // 75% time charging

    bool present = true;
    if (acpi_available && p_acpi_has_battery)
        present = p_acpi_has_battery();

    out->present = present ? 1 : 0;
    out->percent = percent;
    out->charging = charging ? 1 : 0;
    out->remaining_minutes = charging ? (100 - percent) * 2 : percent * 3;
    out->voltage_mv = 12000 + percent * 10;
    out->current_ma = charging ? 1500 : -800;
    strncpy(out->name, "BAT0", sizeof(out->name) - 1);
    if (charging && percent >= 100)
        strncpy(out->status_text, "Charged", sizeof(out->status_text) - 1);
    else if (charging)
        strncpy(out->status_text, "Charging", sizeof(out->status_text) - 1);
    else
        strncpy(out->status_text, "Discharging", sizeof(out->status_text) - 1);
    return true;
}
