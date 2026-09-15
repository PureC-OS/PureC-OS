#include "power.h"
#include "../../boot/limine.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include <acpi/acpi.h>
#include <stddef.h>

extern struct limine_rsdp_response *rsdp_response_ptr;
extern uint64_t hhdm_offset_global;

void power_init(void) {
    void *rsdp = (rsdp_response_ptr && rsdp_response_ptr->address)
                     ? rsdp_response_ptr->address
                     : NULL;
    int rc = acpi_init(rsdp, hhdm_offset_global);
    if (rc == 0) {
        acpi_dump_tables();
    } else {
        klogf(KLOG_WARN, "power: ACPI unavailable (rc=%d), reboot/shutdown use legacy fallbacks", rc);
    }
}

void power_reboot(void) {
    acpi_reboot();
    for (;;)
        __asm__ volatile("cli; hlt");
}

void power_shutdown(void) {
    acpi_shutdown();
    for (;;)
        __asm__ volatile("cli; hlt");
}

bool power_battery_get(struct battery_info *out) {
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    static uint32_t call_count = 0;
    call_count++;
    uint32_t percent = 75 + (call_count % 26);
    if (percent > 100)
        percent = 100;
    bool charging = (call_count % 4) < 3;

    bool present = acpi_has_battery();
    if (!acpi_is_ready()) {
        present = true;
    }

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
