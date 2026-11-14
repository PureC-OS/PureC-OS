#pragma once

#include <stdbool.h>
#include <stdint.h>

struct limine_smp_response;

bool smp_start_cpu(struct limine_smp_response *response, uint32_t logical_id, uint32_t timeout_ms);
void smp_start_all(struct limine_smp_response *response);
void smp_release_scheduler(void);
void smp_reschedule_cpu(uint32_t id);
void smp_reschedule_all(void);
void smp_tlb_shootdown(void);
void smp_tlb_shootdown_space(uint64_t space);
void smp_note_cr3(uint64_t cr3);
bool smp_handle_nmi(void);
void smp_stop_others(void);

