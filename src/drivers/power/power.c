#include "power.h"
#include "../../boot/limine.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/module/kmod.h"
#include "../../lib/string.h"
#include <stddef.h>

extern struct limine_rsdp_response *rsdp_response_ptr;
extern uint64_t hhdm_offset_global;

#define ACPI_MODULE "acpi"
#define ACPI_LIMINE_PATH "/bin/modules/acpi.elf"

struct acpi_battery_abi {
    bool present;
    char name[8];
    uint32_t uid;
    bool percent_valid;
    uint8_t percent;
};
struct acpi_ac_abi {
    bool present;
    char name[8];
    bool online_valid;
    bool online;
};

static int (*p_acpi_init)(void *rsdp, uint64_t hhdm) = NULL;
static void (*p_acpi_dump)(void) = NULL;
static void (*p_acpi_shutdown)(void) = NULL;
static void (*p_acpi_reboot)(void) = NULL;
static bool (*p_acpi_ready)(void) = NULL;
static bool (*p_acpi_has_battery)(void) = NULL;
static bool (*p_acpi_battery_get)(struct acpi_battery_abi *out) = NULL;
static bool (*p_acpi_ac_get)(struct acpi_ac_abi *out) = NULL;
static uint32_t (*p_acpi_power_source)(void) = NULL;
static bool (*p_acpi_ec_present)(void) = NULL;
static bool acpi_available = false;
#define POWER_CACHE_TTL_MS 2000u
static uint64_t cache_filled_ms = 0;
static bool cache_valid = false;
static struct acpi_battery_abi cached_bat;
static struct acpi_ac_abi cached_ac;
static uint32_t cached_source = POWER_SOURCE_UNKNOWN;
static bool cached_ec = false;

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
    p_acpi_battery_get =
        kmod_get_symbol(ACPI_MODULE, "acpi_battery_get");
    p_acpi_ac_get = kmod_get_symbol(ACPI_MODULE, "acpi_ac_get");
    p_acpi_power_source =
        kmod_get_symbol(ACPI_MODULE, "acpi_power_source");
    p_acpi_ec_present =
        kmod_get_symbol(ACPI_MODULE, "acpi_ec_present");
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
    if (p_acpi_ec_present && p_acpi_ec_present()) {
        uint8_t ec_status = inb(0x66);
        klogf(ec_status != 0xFF ? KLOG_OK : KLOG_WARN,
              "power: EC status port 0x66 = 0x%x%s", ec_status,
              ec_status != 0xFF ? " (controller live)" : " (no response)");
    }
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

static void power_cache_refresh(void) {
    uint64_t now = timer_ticks();
    if (cache_valid && now - cache_filled_ms < POWER_CACHE_TTL_MS)
        return;
    memset(&cached_bat, 0, sizeof(cached_bat));
    memset(&cached_ac, 0, sizeof(cached_ac));
    cached_source = POWER_SOURCE_UNKNOWN;
    cached_ec = false;
    if (acpi_available) {
        if (p_acpi_battery_get)
            p_acpi_battery_get(&cached_bat);
        else if (p_acpi_has_battery && p_acpi_has_battery()) {
            cached_bat.present = true;
            strncpy(cached_bat.name, "BAT0", sizeof(cached_bat.name) - 1);
        }
        if (p_acpi_ac_get)
            p_acpi_ac_get(&cached_ac);
        if (p_acpi_power_source)
            cached_source = p_acpi_power_source();
        else if (cached_ac.present && cached_ac.online_valid)
            cached_source = cached_ac.online ? POWER_SOURCE_AC
                                             : POWER_SOURCE_BATTERY;
        if (p_acpi_ec_present)
            cached_ec = p_acpi_ec_present();
    }
    cache_valid = true;
    cache_filled_ms = now;
}

bool power_battery_get(struct battery_info *out) {
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    power_cache_refresh();
    if (!cached_bat.present) {
        out->present = 0;
        out->percent = BATTERY_PERCENT_UNKNOWN;
        strncpy(out->status_text, "No battery",
                sizeof(out->status_text) - 1);
        return true;
    }
    out->present = 1;
    strncpy(out->name, cached_bat.name[0] ? cached_bat.name : "BAT0",
            sizeof(out->name) - 1);
    if (cached_bat.percent_valid) {
        out->percent = cached_bat.percent;
    } else {
        out->percent = BATTERY_PERCENT_UNKNOWN;
    }
    out->remaining_minutes = 0;
    out->voltage_mv = 0;
    out->current_ma = 0;
    if (cached_ac.present && cached_ac.online_valid) {
        out->charging = cached_ac.online ? 1 : 0;
        if (cached_ac.online && out->percent != BATTERY_PERCENT_UNKNOWN &&
            out->percent >= 100)
            strncpy(out->status_text, "Charged",
                    sizeof(out->status_text) - 1);
        else if (cached_ac.online)
            strncpy(out->status_text, "Charging",
                    sizeof(out->status_text) - 1);
        else
            strncpy(out->status_text, "Discharging",
                    sizeof(out->status_text) - 1);
    } else {
        out->charging = 0;
        strncpy(out->status_text, "Unknown",
                sizeof(out->status_text) - 1);
    }
    return true;
}
bool power_ac_get(struct ac_adapter_info *out) {
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    power_cache_refresh();
    if (!cached_ac.present)
        return true; // present=0: no AC device (desktop/VM)
    out->present = 1;
    strncpy(out->name, cached_ac.name[0] ? cached_ac.name : "AC",
            sizeof(out->name) - 1);
    out->online_valid = cached_ac.online_valid ? 1 : 0;
    out->online = cached_ac.online ? 1 : 0;
    return true;
}

// Power source classification: от сети или от батареи.
bool power_source_get(struct power_source_info *out) {
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    power_cache_refresh();
    out->source = cached_source;
    out->battery_present = cached_bat.present ? 1 : 0;
    out->ac_present = cached_ac.present ? 1 : 0;
    out->battery_percent = cached_bat.percent_valid
                               ? cached_bat.percent
                               : BATTERY_PERCENT_UNKNOWN;
    const char *text = "Unknown";
    if (!cached_bat.present && !cached_ac.present)
        text = "No battery";
    else if (cached_source == POWER_SOURCE_AC)
        text = "On AC power";
    else if (cached_source == POWER_SOURCE_BATTERY)
        text = "On battery";
    strncpy(out->status_text, text, sizeof(out->status_text) - 1);
    return true;
}
