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

    if (!present) {
        strncpy(out->status_text, "No battery", sizeof(out->status_text) - 1);
        return true;
    }

    struct acpi_battery_live live;
    bool live_ok = acpi_battery_refresh(&live);
    if (live_ok && live.valid && live.present) {
        out->percent = live.percent;
        out->charging = live.charging;
        out->voltage_mv = live.voltage_mv;
        out->current_ma = live.current_ma;
        out->remaining_minutes = live.remaining_min;
        if (live.model[0]) {
            size_t i = 0;
            while (out->name[i] && i < sizeof(out->name) - 1)
                i++;
            if (i < sizeof(out->name) - 1) {
                out->name[i++] = ' ';
                for (size_t j = 0; live.model[j] && i < sizeof(out->name) - 1; j++)
                    out->name[i++] = live.model[j];
                out->name[i] = '\0';
            }
        }
        const char *st = "OK";
        if (live.charging)
            st = "Charging";
        else if (live.percent >= 99)
            st = "Full";
        else
            st = "Discharging";
        strncpy(out->status_text, st, sizeof(out->status_text) - 1);
        return true;
    }
    if (live_ok && live.valid && !live.present) {
        out->present = 0;
        strncpy(out->name, "none", sizeof(out->name) - 1);
        strncpy(out->status_text, "Not present (_STA)", sizeof(out->status_text) - 1);
        return true;
    }

    if (!acpi_battery_has_bst() && !acpi_battery_has_bif())
        strncpy(out->status_text, "Unknown (no _BIF/_BST)", sizeof(out->status_text) - 1);
    else if (!acpi_uacpi_full_ready())
        strncpy(out->status_text, "Unknown (no AML)", sizeof(out->status_text) - 1);
    else
        strncpy(out->status_text, "Unknown (eval failed)", sizeof(out->status_text) - 1);
    return true;
}
