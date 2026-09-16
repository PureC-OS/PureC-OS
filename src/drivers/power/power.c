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
    bool present = acpi_is_ready() ? acpi_has_battery() : false;

    out->present = present ? 1 : 0;
    out->percent = BATTERY_PERCENT_UNKNOWN;
    out->charging = 0;
    out->remaining_minutes = 0;
    out->voltage_mv = 0;
    out->current_ma = 0;

    const char *aname = acpi_battery_name();
    if (present && aname && aname[0]) {
        strncpy(out->name, aname, sizeof(out->name) - 1);
        out->name[sizeof(out->name) - 1] = '\0';
    } else if (present) {
        strncpy(out->name, "BAT0", sizeof(out->name) - 1);
    } else {
        strncpy(out->name, "none", sizeof(out->name) - 1);
    }

    if (!present)
        strncpy(out->status_text, "No battery", sizeof(out->status_text) - 1);
    else if (!acpi_battery_has_bst() && !acpi_battery_has_bif())
        strncpy(out->status_text, "Unknown (no _BIF/_BST)", sizeof(out->status_text) - 1);
    else
        strncpy(out->status_text, "Unknown (no AML)", sizeof(out->status_text) - 1);
    return true;
}
