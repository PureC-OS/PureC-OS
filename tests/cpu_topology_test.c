#include "kernel/smp/cpu.h"
#include "boot/limine.h"
#include "kernel/diagnostics/klog.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

/* Only the log sink is replaced; discovery runs the kernel implementation. */
void klog(enum klog_level level, const char *message){
    (void)level;
    (void)message;
}

void klogf(enum klog_level level, const char *format, ...){
    (void)level;
    (void)format;
}

static void assert_fallback(void){
    assert(cpu_detected_count() == 1);
    assert(cpu_registered_count() == 1);
    assert(cpu_online_count() == 1);
    const struct cpu_info *bsp = cpu_get_info(0);
    assert(bsp && bsp->is_bsp && !bsp->ids_valid);
    assert(cpu_is_online(0));
    assert(cpu_get_state(0) == CPU_ONLINE);
    assert(cpu_get_info(1) == NULL);
    assert(cpu_get_info(UINT32_MAX) == NULL);
}

static void test_missing_response(void){
    cpu_topology_init(NULL);
    assert_fallback();
    struct limine_smp_response response = {0};
    cpu_topology_init(&response);
    assert_fallback();
    response.cpu_count = 2;
    cpu_topology_init(&response);
    assert_fallback();
}

static void test_cpu_counts_and_limit(void){
    enum { COUNT = CPU_MAX_COUNT + 4 };
    struct limine_smp_info records[COUNT] = {0};
    struct limine_smp_info *pointers[COUNT];
    const uint32_t counts[] = {1, 2, 4, CPU_MAX_COUNT, COUNT};
    for(uint32_t i = 0; i < COUNT; i++){
        records[i].processor_id = 1000 + i;
        records[i].lapic_id = 300 + i * 7;
        records[i].extra_argument = 0x12345678;
        pointers[i] = &records[i];
    }
    for(size_t n = 0; n < sizeof(counts) / sizeof(counts[0]); n++){
        uint32_t count = counts[n];
        uint32_t registered = count < CPU_MAX_COUNT ? count : CPU_MAX_COUNT;
        struct limine_smp_response response = {
            .flags = LIMINE_SMP_X2APIC,
            .bsp_lapic_id = records[count - 1].lapic_id,
            .cpu_count = count,
            .cpus = pointers
        };
        cpu_topology_init(&response);
        assert(cpu_detected_count() == count);
        assert(cpu_registered_count() == registered);
        assert(cpu_online_count() == 1);
        const struct cpu_info *bsp = cpu_get_info(0);
        assert(bsp->logical_id == 0 && bsp->is_bsp && cpu_is_online(0));
        assert(bsp->ids_valid && bsp->lapic_id == response.bsp_lapic_id);
        assert(bsp->processor_id == records[count - 1].processor_id);
        for(uint32_t i = 1; i < registered; i++){
            const struct cpu_info *ap = cpu_get_info(i);
            assert(ap && ap->logical_id == i && ap->ids_valid);
            assert(!ap->is_bsp && !cpu_is_online(i));
            assert(cpu_get_state(i) == CPU_PARKED);
            assert(ap->lapic_id == records[i - 1].lapic_id);
            assert(ap->processor_id == records[i - 1].processor_id);
        }
        assert(cpu_get_info(registered) == NULL);
        for(uint32_t i = 0; i < COUNT; i++){
            assert(records[i].goto_address == NULL);
            assert(records[i].extra_argument == 0x12345678);
        }
    }
    /* Malformed or absent data must discard a previous successful inventory. */
    cpu_topology_init(NULL);
    assert_fallback();
}

static void test_ap_state_machine(void){
    struct limine_smp_info records[] = {
        {.processor_id = 10, .lapic_id = 20},
        {.processor_id = 11, .lapic_id = 21}
    };
    struct limine_smp_info *pointers[] = {&records[0], &records[1]};
    struct limine_smp_response response = {
        .bsp_lapic_id = 20, .cpu_count = 2, .cpus = pointers
    };

    cpu_topology_init(&response);
    assert(cpu_try_start(1));
    assert(!cpu_try_start(1));
    assert(cpu_get_state(1) == CPU_STARTING);
    assert(cpu_publish_idle(1));
    assert(cpu_get_state(1) == CPU_IDLE);
    assert(cpu_is_online(1));
    assert(cpu_online_count() == 2);
    cpu_fail(1);
    assert(cpu_get_state(1) == CPU_FAILED);
    assert(cpu_online_count() == 1);

    cpu_topology_init(&response);
    assert(cpu_try_start(1));
    assert(cpu_timeout_start(1));
    assert(cpu_get_state(1) == CPU_TIMED_OUT);
    assert(!cpu_publish_idle(1));
    assert(!cpu_timeout_start(1));
}

static void test_malformed_lists(void){
    struct limine_smp_info records[] = {
        {.lapic_id = 10}, {.lapic_id = 20}, {.lapic_id = 30}
    };
    struct limine_smp_info *pointers[] = {&records[0], &records[1], &records[2]};
    struct limine_smp_response response = {
        .bsp_lapic_id = 99, .cpu_count = 3, .cpus = pointers
    };
    cpu_topology_init(&response); /* Missing BSP. */
    assert_fallback();
    response.bsp_lapic_id = 10;
    pointers[2] = NULL;
    cpu_topology_init(&response);
    assert_fallback();
    pointers[2] = &records[0]; /* Duplicate BSP. */
    cpu_topology_init(&response);
    assert_fallback();
    pointers[2] = &records[1]; /* Duplicate AP. */
    cpu_topology_init(&response);
    assert_fallback();
    response.cpu_count = (uint64_t)UINT32_MAX + 1;
    cpu_topology_init(&response); /* Must reject before traversing the array. */
    assert_fallback();
}

static void test_ids_are_copied(void){
    struct limine_smp_info bsp = {.processor_id = UINT32_MAX, .lapic_id = UINT32_MAX};
    struct limine_smp_info *pointers[] = {&bsp};
    struct limine_smp_response response = {
        .bsp_lapic_id = UINT32_MAX, .cpu_count = 1, .cpus = pointers
    };
    cpu_topology_init(&response);
    bsp.processor_id = 0;
    bsp.lapic_id = 0;
    assert(cpu_get_info(0)->ids_valid);
    assert(cpu_get_info(0)->processor_id == UINT32_MAX);
    assert(cpu_get_info(0)->lapic_id == UINT32_MAX);
}

int main(void){
    test_missing_response();
    test_cpu_counts_and_limit();
    test_malformed_lists();
    test_ids_are_copied();
    test_ap_state_machine();
    puts("CPU topology tests passed");
    return 0;
}
