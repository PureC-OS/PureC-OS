#include "cpu.h"
#include "../../boot/limine.h"
#include "../diagnostics/klog.h"

#include <stddef.h>

static struct cpu_info cpus[CPU_MAX_COUNT] = {
    {.logical_id = 0, .is_bsp = true, .online = true}
};
static uint32_t detected_count = 1;
static uint32_t registered_count = 1;

static void use_bsp_only(void){
    cpus[0] = (struct cpu_info){
        .logical_id = 0, .is_bsp = true, .online = true
    };
    detected_count = 1;
    registered_count = 1;
}

static void register_cpu(const struct limine_smp_info *info, bool is_bsp){
    uint32_t id = registered_count++;
    cpus[id] = (struct cpu_info){
        .logical_id = id,
        .processor_id = info->processor_id,
        .lapic_id = info->lapic_id,
        .ids_valid = true,
        .is_bsp = is_bsp,
        .online = is_bsp
    };
}

static bool discover_limine(const struct limine_smp_response *response){
    if(!response || !response->cpus || !response->cpu_count
       || response->cpu_count > UINT32_MAX) return false;

    /* Find the BSP before applying our table limit: firmware order and APIC
       IDs need not start at zero or be contiguous. */
    const struct limine_smp_info *bsp = NULL;
    for(uint64_t i = 0; i < response->cpu_count; i++){
        const struct limine_smp_info *info = response->cpus[i];
        if(!info) return false;
        if(info->lapic_id == response->bsp_lapic_id){
            if(bsp) return false;
            bsp = info;
        }
    }
    if(!bsp) return false;

    registered_count = 0;
    register_cpu(bsp, true);
    for(uint64_t i = 0; i < response->cpu_count
        && registered_count < CPU_MAX_COUNT; i++){
        const struct limine_smp_info *info = response->cpus[i];
        if(info == bsp) continue;
        for(uint32_t j = 0; j < registered_count; j++){
            if(cpus[j].lapic_id == info->lapic_id) return false;
        }
        register_cpu(info, false);
    }
    detected_count = (uint32_t)response->cpu_count;
    return true;
}

void cpu_topology_init(const struct limine_smp_response *response){
    use_bsp_only();
    if(!discover_limine(response)){
        use_bsp_only();
        klog(KLOG_WARN, "smp: missing or invalid Limine CPU list; using BSP only (APIC ID unknown)");
    }
    klogf(KLOG_INFO, "smp: detected=%u registered=%u online=%u source=%s",
          detected_count, registered_count, cpu_online_count(),
          cpus[0].ids_valid ? "Limine" : "BSP fallback");
    if(detected_count > registered_count){
        klogf(KLOG_WARN, "smp: CPU table limited to %u entries; %u CPUs not registered",
              registered_count, detected_count - registered_count);
    }
    for(uint32_t i = 0; i < registered_count; i++){
        const struct cpu_info *cpu = &cpus[i];
        if(!cpu->ids_valid) continue;
        klogf(KLOG_INFO, "smp: cpu%u processor_id=%u lapic_id=%u %s %s",
              cpu->logical_id, cpu->processor_id, cpu->lapic_id,
              cpu->is_bsp ? "BSP" : "AP", cpu->online ? "online" : "parked");
    }
}

uint32_t cpu_detected_count(void){ return detected_count; }
uint32_t cpu_registered_count(void){ return registered_count; }

uint32_t cpu_online_count(void){
    uint32_t count = 0;
    for(uint32_t i = 0; i < registered_count; i++){
        if(cpus[i].online) count++;
    }
    return count;
}

const struct cpu_info *cpu_get_info(uint32_t logical_id){
    return logical_id < registered_count ? &cpus[logical_id] : NULL;
}
