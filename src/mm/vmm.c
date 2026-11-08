#include "vmm.h"
#include "../kernel/smp/smp.h"
#include "../kernel/sync/spinlock.h"
#include "../kernel/diagnostics/klog.h"
#include "../lib/string.h"
#include "oom/oom_pressure.h"
#include "pmm.h"
#include "../io/swap.h"

static spinlock_t vmm_lock = SPINLOCK_INIT;
static uint64_t vmm_create_address_space_locked(void);
static void vmm_destroy_address_space_locked(uint64_t address_space);
static bool vmm_map_page_locked(uint64_t address_space,
                                uint64_t virtual_address,
                                uint64_t physical_address, uint64_t flags);
static bool vmm_map_new_pages_locked(uint64_t address_space,
                                     uint64_t virtual_address,
                                     uint64_t page_count, uint64_t flags);
static bool vmm_protect_page_locked(uint64_t address_space,
                                    uint64_t virtual_address, uint64_t flags);
static uint64_t vmm_translate_locked(uint64_t address_space,
                                     uint64_t virtual_address);
static bool vmm_user_range_accessible_locked(uint64_t address_space,
                                             uint64_t address, uint64_t size,
                                             bool writable);
static uint64_t vmm_user_page_count_locked(uint64_t address_space);

#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
static void vmm_destroy_table_level(uint64_t physical, unsigned level);
static bool cow_resolve_slot(uint64_t *slot, uint64_t virtual_address);
static bool swap_in_locked(uint64_t *slot, uint64_t virtual_address);
static bool swap_out_one_locked(void);
static void space_register(uint64_t address_space);
static void space_unregister(uint64_t address_space);
#define SWAP_ENTRY_KEEP (VMM_PAGE_WRITABLE | VMM_PAGE_USER | VMM_PAGE_NX | \
                         VMM_PAGE_COW | VMM_PAGE_ANON)
#define SWAP_SLOT_OF(entry) ((uint32_t)(((entry) & PAGE_ADDRESS_MASK) >> 12))
#define VMM_PAGE_ACCESSED (1ULL << 5)
#define VMM_SPACE_MAX 1024
#define USER_TOP 0x0000800000000000ULL

static uint64_t kernel_address_space;
static bool nx_enabled;

static bool enable_nx_cpu(void)
{
  uint32_t maximum, eax, ebx, ecx, edx;
  __asm__ volatile("cpuid"
                   : "=a"(maximum), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0x80000000U), "c"(0));
  if (maximum < 0x80000001U)
    return false;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0x80000001U), "c"(0));
  if (!(edx & (1U << 20)))
    return false;
  uint32_t low, high;
  __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080U));
  low |= 1U << 11;
  __asm__ volatile("wrmsr" ::"a"(low), "d"(high), "c"(0xC0000080U));
  return true;
}

#define CR0_WP (1ULL << 16)

static void enable_write_protect_cpu(void)
{
  uint64_t value;
  __asm__ volatile("mov %%cr0,%0" : "=r"(value));
  if (!(value & CR0_WP))
  {
    value |= CR0_WP;
    __asm__ volatile("mov %0,%%cr0" ::"r"(value) : "memory");
  }
}

static uint64_t read_cr3(void)
{
  uint64_t value;
  __asm__ volatile("mov %%cr3,%0" : "=r"(value));
  return value & PAGE_ADDRESS_MASK;
}

static uint64_t *table_from_entry(uint64_t entry)
{
  return (uint64_t *)pmm_physical_to_virtual(entry & PAGE_ADDRESS_MASK);
}

static uint64_t *next_table(uint64_t *table, uint16_t index, bool create,
                            uint64_t flags)
{
  if (table[index] & VMM_PAGE_PRESENT)
    return table_from_entry(table[index]);
  if (!create)
    return 0;
  uint64_t physical = pmm_allocate_page();
  if (!physical)
    return 0;
  table[index] =
      physical | VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | (flags & VMM_PAGE_USER);
  return (uint64_t *)pmm_physical_to_virtual(physical);
}

void vmm_init(void)
{
  kernel_address_space = read_cr3();
  nx_enabled = enable_nx_cpu();
  enable_write_protect_cpu();
  klogf(KLOG_OK, "vmm: kernel cr3=0x%llx nx=%s", kernel_address_space,
        nx_enabled ? "on" : "unsupported");
}

bool vmm_init_cpu(void)
{
  if (!kernel_address_space)
    return false;
  if (nx_enabled && !enable_nx_cpu())
    return false;
  enable_write_protect_cpu();
  vmm_switch_address_space(kernel_address_space);
  return true;
}

uint64_t vmm_kernel_address_space(void) { return kernel_address_space; }

static void invalidate_local_page(uint64_t virtual_address)
{
  __asm__ volatile("invlpg (%0)" ::"r"(virtual_address) : "memory");
}

static bool cow_resolve_slot(uint64_t *slot, uint64_t virtual_address)
{
  uint64_t entry = *slot;
  if (!(entry & VMM_PAGE_PRESENT) || !(entry & VMM_PAGE_USER))
    return false;
  if (entry & VMM_PAGE_WRITABLE)
    return true;
  if (!(entry & VMM_PAGE_COW))
    return false;
  uint64_t frame = entry & PAGE_ADDRESS_MASK;
  if (!pmm_frame_shared(frame))
  {
    *slot = (entry & ~VMM_PAGE_COW) | VMM_PAGE_WRITABLE;
    invalidate_local_page(virtual_address);
    return true;
  }
  uint64_t copy = pmm_allocate_page();
  if (!copy)
    return false;
  memcpy(pmm_physical_to_virtual(copy), pmm_physical_to_virtual(frame),
         PMM_PAGE_SIZE);
  *slot = (entry & ~(PAGE_ADDRESS_MASK | VMM_PAGE_COW)) | copy |
          VMM_PAGE_WRITABLE;
  invalidate_local_page(virtual_address);
  pmm_free_page(frame);
  return true;
}

static uint64_t *user_slot_locked(uint64_t address_space,
                                  uint64_t virtual_address)
{
  uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(address_space);
  uint64_t *pdpt =
      next_table(pml4, (virtual_address >> 39) & 0x1FF, false, 0);
  if (!pdpt)
    return 0;
  uint64_t *pd = next_table(pdpt, (virtual_address >> 30) & 0x1FF, false, 0);
  if (!pd)
    return 0;
  uint64_t *pt = next_table(pd, (virtual_address >> 21) & 0x1FF, false, 0);
  if (!pt)
    return 0;
  return &pt[(virtual_address >> 12) & 0x1FF];
}

static uint64_t clone_table_level(uint64_t source_physical, unsigned level)
{
  uint64_t physical = pmm_allocate_page();
  if (!physical)
    return 0;
  uint64_t *source = (uint64_t *)pmm_physical_to_virtual(source_physical);
  uint64_t *destination = (uint64_t *)pmm_physical_to_virtual(physical);
  for (uint16_t index = 0; index < 512; index++)
  {
    uint64_t entry = source[index];
    if (!(entry & VMM_PAGE_PRESENT))
    {
      if (level == 1 && (entry & VMM_PAGE_SWAPENT))
      {
        if (!swap_dup(SWAP_SLOT_OF(entry)))
        {
          vmm_destroy_table_level(physical, level);
          return 0;
        }
        destination[index] = entry;
      }
      continue;
    }
    if (level == 1)
    {
      uint64_t frame = entry & PAGE_ADDRESS_MASK;
      if (!pmm_frame_ref(frame))
      {
        vmm_destroy_table_level(physical, level);
        return 0;
      }
      if ((entry & (VMM_PAGE_WRITABLE | VMM_PAGE_USER)) ==
          (VMM_PAGE_WRITABLE | VMM_PAGE_USER))
      {
        entry = (entry & ~VMM_PAGE_WRITABLE) | VMM_PAGE_COW;
        source[index] = entry;
      }
      destination[index] = entry;
      continue;
    }
    uint64_t child = clone_table_level(entry & PAGE_ADDRESS_MASK, level - 1);
    if (!child)
    {
      vmm_destroy_table_level(physical, level);
      return 0;
    }
    destination[index] = child | (entry & ~PAGE_ADDRESS_MASK);
  }
  return physical;
}

static void vmm_destroy_table_level(uint64_t physical, unsigned level)
{
  uint64_t *table = (uint64_t *)pmm_physical_to_virtual(physical);
  for (uint16_t index = 0; index < 512; index++)
  {
    if (!(table[index] & VMM_PAGE_PRESENT))
    {
      if (level == 1 && (table[index] & VMM_PAGE_SWAPENT))
        swap_free(SWAP_SLOT_OF(table[index]));
      continue;
    }
    if (level == 1)
      pmm_free_page(table[index] & PAGE_ADDRESS_MASK);
    else
      vmm_destroy_table_level(table[index] & PAGE_ADDRESS_MASK, level - 1);
  }
  pmm_free_page(physical);
}

static uint64_t vmm_create_address_space_locked(void)
{
  uint64_t physical = pmm_allocate_page();
  if (!physical)
    return 0;
  uint64_t *destination = (uint64_t *)pmm_physical_to_virtual(physical);
  uint64_t *kernel = (uint64_t *)pmm_physical_to_virtual(kernel_address_space);
  for (uint16_t index = 256; index < 512; index++)
    destination[index] = kernel[index];
  space_register(physical);
  return physical;
}

static void vmm_destroy_address_space_locked(uint64_t address_space)
{
  if (!address_space || address_space == kernel_address_space)
    return;
  space_unregister(address_space);
  uint64_t *root = (uint64_t *)pmm_physical_to_virtual(address_space);
  uint64_t detached[256];
  for (unsigned i = 0; i < 256; i++)
  {
    detached[i] = root[i];
    root[i] = 0;
  }
  smp_tlb_shootdown(); /* Acknowledge before returning any frame to PMM. */
  uint64_t *pml4 = detached;
  for (uint16_t pml4_index = 0; pml4_index < 256; pml4_index++)
  {
    if (!(pml4[pml4_index] & VMM_PAGE_PRESENT))
      continue;
    uint64_t pdpt_physical = pml4[pml4_index] & PAGE_ADDRESS_MASK;
    uint64_t *pdpt = table_from_entry(pml4[pml4_index]);
    for (uint16_t pdpt_index = 0; pdpt_index < 512; pdpt_index++)
    {
      if (!(pdpt[pdpt_index] & VMM_PAGE_PRESENT))
        continue;
      uint64_t pd_physical = pdpt[pdpt_index] & PAGE_ADDRESS_MASK;
      uint64_t *pd = table_from_entry(pdpt[pdpt_index]);
      for (uint16_t pd_index = 0; pd_index < 512; pd_index++)
      {
        if (!(pd[pd_index] & VMM_PAGE_PRESENT))
          continue;
        uint64_t pt_physical = pd[pd_index] & PAGE_ADDRESS_MASK;
        uint64_t *pt = table_from_entry(pd[pd_index]);
        for (uint16_t pt_index = 0; pt_index < 512; pt_index++)
        {
          if (pt[pt_index] & VMM_PAGE_PRESENT)
            pmm_free_page(pt[pt_index] & PAGE_ADDRESS_MASK);
          else if (pt[pt_index] & VMM_PAGE_SWAPENT)
            swap_free(SWAP_SLOT_OF(pt[pt_index]));
        }
        pmm_free_page(pt_physical);
      }
      pmm_free_page(pd_physical);
    }
    pmm_free_page(pdpt_physical);
  }
  pmm_free_page(address_space);
}

static bool vmm_map_page_locked(uint64_t address_space,
                                uint64_t virtual_address,
                                uint64_t physical_address, uint64_t flags)
{
  if ((virtual_address & (PMM_PAGE_SIZE - 1)) ||
      (physical_address & (PMM_PAGE_SIZE - 1)))
    return false;
  uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(address_space);
  uint64_t *pdpt =
      next_table(pml4, (virtual_address >> 39) & 0x1FF, true, flags);
  if (!pdpt)
    return false;
  uint64_t *pd = next_table(pdpt, (virtual_address >> 30) & 0x1FF, true, flags);
  if (!pd)
    return false;
  uint64_t *pt = next_table(pd, (virtual_address >> 21) & 0x1FF, true, flags);
  if (!pt)
    return false;
  uint16_t index = (virtual_address >> 12) & 0x1FF;
  if (pt[index] & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT))
    return false;
  pt[index] = (physical_address & PAGE_ADDRESS_MASK) | VMM_PAGE_PRESENT |
              (flags & (VMM_PAGE_WRITABLE | VMM_PAGE_USER | VMM_PAGE_ANON)) |
              (nx_enabled ? flags & VMM_PAGE_NX : 0);
  return true;
}

static bool vmm_map_new_pages_locked(uint64_t address_space,
                                     uint64_t virtual_address,
                                     uint64_t page_count, uint64_t flags)
{
  if (!page_count)
    return false;
  if ((flags & VMM_PAGE_USER) && !oom_pressure_user_can_alloc(page_count))
    return false;
  for (uint64_t page = 0; page < page_count; page++)
  {
    uint64_t physical = pmm_allocate_page();
    if (!physical)
      return false;
    if (!vmm_map_page_locked(address_space,
                             virtual_address + page * PMM_PAGE_SIZE, physical,
                             flags | ((flags & VMM_PAGE_USER) ? VMM_PAGE_ANON : 0)))
    {
      pmm_free_page(physical);
      return false;
    }
  }
  return true;
}

static bool vmm_protect_page_locked(uint64_t address_space,
                                    uint64_t virtual_address, uint64_t flags)
{
  if (virtual_address & (PMM_PAGE_SIZE - 1))
    return false;
  uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(address_space);
  uint64_t *pdpt = next_table(pml4, (virtual_address >> 39) & 0x1FF, false, 0);
  if (!pdpt)
    return false;
  uint64_t *pd = next_table(pdpt, (virtual_address >> 30) & 0x1FF, false, 0);
  if (!pd)
    return false;
  uint64_t *pt = next_table(pd, (virtual_address >> 21) & 0x1FF, false, 0);
  if (!pt)
    return false;
  uint16_t index = (virtual_address >> 12) & 0x1FF;
  if ((pt[index] & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)) == VMM_PAGE_SWAPENT &&
      !swap_in_locked(&pt[index], virtual_address))
    return false;
  uint64_t entry = pt[index];
  if (!(entry & VMM_PAGE_PRESENT))
    return false;
  pt[index] = (entry & PAGE_ADDRESS_MASK) | VMM_PAGE_PRESENT |
              (entry & VMM_PAGE_ANON) |
              (flags & (VMM_PAGE_WRITABLE | VMM_PAGE_USER)) |
              (nx_enabled ? flags & VMM_PAGE_NX : 0);
  return true;
}

static uint64_t vmm_translate_locked(uint64_t address_space,
                                     uint64_t virtual_address)
{
  uint64_t *table = (uint64_t *)pmm_physical_to_virtual(address_space);
  uint16_t indices[4] = {(uint16_t)((virtual_address >> 39) & 0x1FF),
                         (uint16_t)((virtual_address >> 30) & 0x1FF),
                         (uint16_t)((virtual_address >> 21) & 0x1FF),
                         (uint16_t)((virtual_address >> 12) & 0x1FF)};
  for (uint8_t level = 0; level < 3; level++)
  {
    uint64_t entry = table[indices[level]];
    if (!(entry & VMM_PAGE_PRESENT))
      return 0;
    table = table_from_entry(entry);
  }
  if ((table[indices[3]] & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)) ==
          VMM_PAGE_SWAPENT &&
      !swap_in_locked(&table[indices[3]], virtual_address & ~(PMM_PAGE_SIZE - 1)))
    return 0;
  uint64_t entry = table[indices[3]];
  if (!(entry & VMM_PAGE_PRESENT))
    return 0;
  return (entry & PAGE_ADDRESS_MASK) | (virtual_address & (PMM_PAGE_SIZE - 1));
}

static bool vmm_user_range_accessible_locked(uint64_t address_space,
                                             uint64_t address, uint64_t size,
                                             bool writable)
{
  if (!size)
    return true;
  if (address >= USER_TOP || size > USER_TOP - address)
    return false;
  uint64_t first = address & ~(PMM_PAGE_SIZE - 1);
  uint64_t last = (address + size - 1) & ~(PMM_PAGE_SIZE - 1);
  uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(address_space);
  for (uint64_t page = first;; page += PMM_PAGE_SIZE)
  {
    uint64_t *pdpt = next_table(pml4, (page >> 39) & 0x1FF, false, 0);
    if (!pdpt)
      return false;
    uint64_t *pd = next_table(pdpt, (page >> 30) & 0x1FF, false, 0);
    if (!pd)
      return false;
    uint64_t *pt = next_table(pd, (page >> 21) & 0x1FF, false, 0);
    if (!pt)
      return false;
    uint64_t *slot = &pt[(page >> 12) & 0x1FF];
    if ((*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)) == VMM_PAGE_SWAPENT &&
        !swap_in_locked(slot, page))
      return false;
    uint64_t entry = *slot;
    if (!(entry & VMM_PAGE_PRESENT) || !(entry & VMM_PAGE_USER))
      return false;
    if (writable && !(entry & VMM_PAGE_WRITABLE) &&
        !cow_resolve_slot(slot, page))
      return false;
    if (page == last)
      break;
  }
  return true;
}

static uint64_t vmm_user_page_count_locked(uint64_t address_space)
{
  if (!address_space || address_space == kernel_address_space)
    return 0;
  uint64_t count = 0;
  uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(address_space);
  for (uint16_t pml4_index = 0; pml4_index < 256; pml4_index++)
  {
    if (!(pml4[pml4_index] & VMM_PAGE_PRESENT))
      continue;
    uint64_t *pdpt = table_from_entry(pml4[pml4_index]);
    for (uint16_t pdpt_index = 0; pdpt_index < 512; pdpt_index++)
    {
      if (!(pdpt[pdpt_index] & VMM_PAGE_PRESENT))
        continue;
      uint64_t *pd = table_from_entry(pdpt[pdpt_index]);
      for (uint16_t pd_index = 0; pd_index < 512; pd_index++)
      {
        if (!(pd[pd_index] & VMM_PAGE_PRESENT))
          continue;
        uint64_t *pt = table_from_entry(pd[pd_index]);
        for (uint16_t pt_index = 0; pt_index < 512; pt_index++)
          if ((pt[pt_index] & (VMM_PAGE_PRESENT | VMM_PAGE_USER)) ==
              (VMM_PAGE_PRESENT | VMM_PAGE_USER))
            count++;
      }
    }
  }
  return count;
}

void vmm_switch_address_space(uint64_t address_space)
{
  if (!address_space || read_cr3() == address_space)
    return;
  __asm__ volatile("mov %0,%%cr3" ::"r"(address_space) : "memory");
}

bool vmm_protect_page(uint64_t address_space, uint64_t virtual_address,
                      uint64_t flags)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  bool result = vmm_protect_page_locked(address_space, virtual_address, flags);
  smp_tlb_shootdown();
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

uint64_t vmm_create_address_space(void)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t result = vmm_create_address_space_locked();
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

void vmm_destroy_address_space(uint64_t address_space)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  vmm_destroy_address_space_locked(address_space);
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
}

bool vmm_map_page(uint64_t address_space, uint64_t virtual_address,
                  uint64_t physical_address, uint64_t flags)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  bool result = vmm_map_page_locked(address_space, virtual_address,
                                    physical_address, flags);
  smp_tlb_shootdown();
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

bool vmm_map_new_pages(uint64_t address_space, uint64_t virtual_address,
                       uint64_t page_count, uint64_t flags)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  bool result = vmm_map_new_pages_locked(address_space, virtual_address,
                                         page_count, flags);
  smp_tlb_shootdown();
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

uint64_t vmm_translate(uint64_t address_space, uint64_t virtual_address)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t result = vmm_translate_locked(address_space, virtual_address);
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

bool vmm_user_range_accessible(uint64_t address_space, uint64_t address,
                               uint64_t size, bool writable)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  bool result =
      vmm_user_range_accessible_locked(address_space, address, size, writable);
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

uint64_t vmm_user_page_count(uint64_t address_space)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t result = vmm_user_page_count_locked(address_space);
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

bool vmm_user_address(uint64_t virtual_address)
{
  return virtual_address < USER_TOP;
}

uint64_t vmm_clone_address_space(uint64_t source)
{
  if (!source || source == kernel_address_space)
    return 0;
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t root = vmm_create_address_space_locked();
  uint64_t result = 0;
  if (root)
  {
    uint64_t *src = (uint64_t *)pmm_physical_to_virtual(source);
    uint64_t *dst = (uint64_t *)pmm_physical_to_virtual(root);
    bool ok = true;
    for (uint16_t index = 0; index < 256 && ok; index++)
    {
      if (!(src[index] & VMM_PAGE_PRESENT))
        continue;
      uint64_t child = clone_table_level(src[index] & PAGE_ADDRESS_MASK, 3);
      if (!child)
      {
        ok = false;
        break;
      }
      dst[index] = child | (src[index] & ~PAGE_ADDRESS_MASK);
    }
    if (ok)
      result = root;
    else
      vmm_destroy_address_space_locked(root);
  }
  smp_tlb_shootdown();
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

bool vmm_handle_cow_fault(uint64_t address_space, uint64_t virtual_address,
                          uint64_t error_code)
{
  if (!address_space || address_space == kernel_address_space ||
      virtual_address >= USER_TOP)
    return false;
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t *slot = user_slot_locked(address_space,
                                    virtual_address & ~(PMM_PAGE_SIZE - 1));
  if (slot && (*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)) ==
                  VMM_PAGE_SWAPENT)
  {
    bool swapped = swap_in_locked(slot, virtual_address & ~(PMM_PAGE_SIZE - 1));
    spin_unlock_irqrestore(&vmm_lock, irq_flags);
    return swapped;
  }
  if (!(error_code & 1) || !(error_code & 2))
  {
    spin_unlock_irqrestore(&vmm_lock, irq_flags);
    return false;
  }
  bool result = slot && cow_resolve_slot(slot,
                                         virtual_address & ~(PMM_PAGE_SIZE - 1));
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

bool vmm_map_new_pages_lazy(uint64_t address_space, uint64_t virtual_address,
                            uint64_t page_count, uint64_t flags)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  bool result = vmm_map_new_pages_locked(address_space, virtual_address,
                                         page_count, flags);
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

int vmm_map_zero_page(uint64_t address_space, uint64_t virtual_address,
                      uint64_t flags)
{
  if (!address_space || address_space == kernel_address_space ||
      virtual_address >= USER_TOP || (virtual_address & (PMM_PAGE_SIZE - 1)))
    return -1;
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t *slot = user_slot_locked(address_space, virtual_address);
  if (slot && (*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)))
  {
    spin_unlock_irqrestore(&vmm_lock, irq_flags);
    return 0;
  }
  bool ok = vmm_map_new_pages_locked(address_space, virtual_address, 1, flags);
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return ok ? 1 : -1;
}

static uint64_t *user_slot_skip_locked(uint64_t address_space,
                                       uint64_t virtual_address,
                                       uint64_t *next)
{
  uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(address_space);
  uint64_t *pdpt = next_table(pml4, (virtual_address >> 39) & 0x1FF, false, 0);
  if (!pdpt)
  {
    *next = (virtual_address | ((1ULL << 39) - 1)) + 1;
    return 0;
  }
  uint64_t *pd = next_table(pdpt, (virtual_address >> 30) & 0x1FF, false, 0);
  if (!pd)
  {
    *next = (virtual_address | ((1ULL << 30) - 1)) + 1;
    return 0;
  }
  uint64_t *pt = next_table(pd, (virtual_address >> 21) & 0x1FF, false, 0);
  if (!pt)
  {
    *next = (virtual_address | ((1ULL << 21) - 1)) + 1;
    return 0;
  }
  *next = virtual_address + PMM_PAGE_SIZE;
  return &pt[(virtual_address >> 12) & 0x1FF];
}

static void vmm_page_prot_locked(uint64_t *slot, uint64_t flags)
{
  uint64_t entry = *slot;
  uint64_t base = entry & ~(VMM_PAGE_WRITABLE | VMM_PAGE_USER | VMM_PAGE_NX);
  if (flags & VMM_PAGE_USER)
    base |= VMM_PAGE_USER;
  if (nx_enabled && (flags & VMM_PAGE_NX))
    base |= VMM_PAGE_NX;
  if ((flags & VMM_PAGE_WRITABLE) && !(entry & VMM_PAGE_COW))
    base |= VMM_PAGE_WRITABLE;
  *slot = base;
}

static bool range_valid(uint64_t address_space, uint64_t virtual_address,
                        uint64_t page_count)
{
  return address_space && address_space != kernel_address_space &&
         !(virtual_address & (PMM_PAGE_SIZE - 1)) &&
         virtual_address < USER_TOP &&
         page_count <= (USER_TOP - virtual_address) / PMM_PAGE_SIZE;
}

uint64_t vmm_unmap_range(uint64_t address_space, uint64_t virtual_address,
                         uint64_t page_count)
{
  if (!range_valid(address_space, virtual_address, page_count))
    return 0;
  uint64_t end = virtual_address + page_count * PMM_PAGE_SIZE;
  uint64_t freed = 0;
  uint64_t frames[64];
  uint64_t held = 0;
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t address = virtual_address;
  while (address < end)
  {
    uint64_t next;
    uint64_t *slot = user_slot_skip_locked(address_space, address, &next);
    if (slot && (*slot & VMM_PAGE_PRESENT))
    {
      frames[held++] = *slot & PAGE_ADDRESS_MASK;
      *slot = 0;
      invalidate_local_page(address);
      freed++;
    }
    else if (slot && (*slot & VMM_PAGE_SWAPENT))
    {
      swap_free(SWAP_SLOT_OF(*slot));
      *slot = 0;
      freed++;
    }
    address = next;
    if (held == 64 || (address >= end && held))
    {
      smp_tlb_shootdown();
      for (uint64_t i = 0; i < held; i++)
        pmm_free_page(frames[i]);
      held = 0;
    }
  }
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return freed;
}

uint64_t vmm_present_count(uint64_t address_space, uint64_t virtual_address,
                           uint64_t page_count)
{
  if (!range_valid(address_space, virtual_address, page_count))
    return 0;
  uint64_t end = virtual_address + page_count * PMM_PAGE_SIZE;
  uint64_t present = 0;
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t address = virtual_address;
  while (address < end)
  {
    uint64_t next;
    uint64_t *slot = user_slot_skip_locked(address_space, address, &next);
    if (slot && (*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)))
      present++;
    address = next;
  }
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return present;
}

bool vmm_protect_range(uint64_t address_space, uint64_t virtual_address,
                       uint64_t page_count, uint64_t flags, bool populate)
{
  if (!range_valid(address_space, virtual_address, page_count))
    return false;
  uint64_t end = virtual_address + page_count * PMM_PAGE_SIZE;
  bool ok = true;
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t address = virtual_address;
  while (address < end && ok)
  {
    uint64_t next;
    uint64_t *slot = user_slot_skip_locked(address_space, address, &next);
    if (slot && (*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)) ==
                    VMM_PAGE_SWAPENT &&
        !swap_in_locked(slot, address))
    {
      ok = false;
      break;
    }
    if (slot && (*slot & VMM_PAGE_PRESENT))
    {
      vmm_page_prot_locked(slot, flags);
      invalidate_local_page(address);
    }
    else if (populate)
    {
      ok = vmm_map_new_pages_locked(address_space, address, 1, flags);
      next = address + PMM_PAGE_SIZE;
    }
    address = next;
  }
  smp_tlb_shootdown();
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return ok;
}

uint64_t vmm_page_flags(uint64_t address_space, uint64_t virtual_address)
{
  if (!range_valid(address_space, virtual_address & ~(PMM_PAGE_SIZE - 1), 1))
    return 0;
  uint64_t result = 0;
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  uint64_t *slot = user_slot_locked(address_space,
                                    virtual_address & ~(PMM_PAGE_SIZE - 1));
  if (slot && (*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)))
  {
    uint64_t entry = *slot;
    result = VMM_PAGE_PRESENT | (entry & (VMM_PAGE_USER | VMM_PAGE_NX));
    if (entry & (VMM_PAGE_WRITABLE | VMM_PAGE_COW))
      result |= VMM_PAGE_WRITABLE;
  }
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}


static uint64_t vmm_spaces[VMM_SPACE_MAX];
static uint32_t vmm_space_count;
static uint32_t swap_cursor_space;
static uint64_t swap_cursor_address;
static volatile uint32_t swap_pause_count;

static void space_register(uint64_t address_space)
{
  if (vmm_space_count < VMM_SPACE_MAX)
    vmm_spaces[vmm_space_count++] = address_space;
}

static void space_unregister(uint64_t address_space)
{
  for (uint32_t i = 0; i < vmm_space_count; i++)
  {
    if (vmm_spaces[i] != address_space)
      continue;
    vmm_spaces[i] = vmm_spaces[--vmm_space_count];
    if (swap_cursor_space >= vmm_space_count)
    {
      swap_cursor_space = 0;
      swap_cursor_address = 0;
    }
    return;
  }
}

static bool swap_in_locked(uint64_t *slot, uint64_t virtual_address)
{
  (void)virtual_address;
  uint64_t entry = *slot;
  if ((entry & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)) != VMM_PAGE_SWAPENT)
    return false;
  uint32_t id = SWAP_SLOT_OF(entry);
  uint64_t frame = pmm_allocate_page();
  if (!frame && swap_out_one_locked())
    frame = pmm_allocate_page();
  if (!frame)
    return false;
  if (!swap_read(id, pmm_physical_to_virtual(frame)))
  {
    pmm_free_page(frame);
    klogf(KLOG_ERROR, "swap: read slot %u failed", id);
    return false;
  }
  *slot = (frame & PAGE_ADDRESS_MASK) | VMM_PAGE_PRESENT |
          (entry & SWAP_ENTRY_KEEP);
  swap_free(id);
  return true;
}

static bool swap_out_one_locked(void)
{
  if (!swap_enabled() || swap_pause_count || !vmm_space_count)
    return false;
  for (uint32_t budget = 8192; budget; budget--)
  {
    if (swap_cursor_space >= vmm_space_count)
    {
      swap_cursor_space = 0;
      swap_cursor_address = 0;
    }
    if (swap_cursor_address >= USER_TOP)
    {
      swap_cursor_space++;
      swap_cursor_address = 0;
      continue;
    }
    uint64_t address_space = vmm_spaces[swap_cursor_space];
    uint64_t address = swap_cursor_address;
    uint64_t next;
    uint64_t *slot = user_slot_skip_locked(address_space, address, &next);
    swap_cursor_address = next;
    if (!slot)
      continue;
    uint64_t entry = *slot;
    const uint64_t need = VMM_PAGE_PRESENT | VMM_PAGE_USER | VMM_PAGE_ANON;
    if ((entry & need) != need)
      continue;
    uint64_t frame = entry & PAGE_ADDRESS_MASK;
    if (pmm_frame_shared(frame))
      continue;
    if (entry & VMM_PAGE_ACCESSED)
    {
      *slot = entry & ~VMM_PAGE_ACCESSED;
      continue;
    }
    uint32_t id = swap_alloc();
    if (!id)
      return false;
    *slot = VMM_PAGE_SWAPENT | ((uint64_t)id << 12) | (entry & SWAP_ENTRY_KEEP);
    invalidate_local_page(address);
    smp_tlb_shootdown();
    if (!swap_write(id, pmm_physical_to_virtual(frame)))
    {
      *slot = entry;
      swap_free(id);
      klogf(KLOG_ERROR, "swap: write slot %u failed", id);
      return false;
    }
    pmm_free_page(frame);
    return true;
  }
  return false;
}

uint32_t vmm_swap_reclaim(uint32_t want_pages)
{
  uint32_t done = 0;
  while (done < want_pages)
  {
    uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
    bool ok = swap_out_one_locked();
    spin_unlock_irqrestore(&vmm_lock, irq_flags);
    if (!ok)
      break;
    done++;
  }
  return done;
}

void vmm_swap_pause(void)
{
  __atomic_add_fetch(&swap_pause_count, 1, __ATOMIC_ACQ_REL);
}

void vmm_swap_resume(void)
{
  __atomic_sub_fetch(&swap_pause_count, 1, __ATOMIC_ACQ_REL);
}
