#include "vmm.h"
#include <arch/arch.h>
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
static bool range_valid(uint64_t address_space, uint64_t virtual_address,
                        uint64_t page_count);
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
static uint64_t *user_slot_locked(uint64_t address_space,
                                  uint64_t virtual_address);
static uint64_t *user_slot_skip_locked(uint64_t address_space,
                                       uint64_t virtual_address,
                                       uint64_t *next);

#define PAGE_ADDRESS_MASK ARCH_PTE_ADDR_MASK
static bool cow_resolve_slot(uint64_t *slot, uint64_t virtual_address);
static bool swap_in_locked(uint64_t *slot, uint64_t virtual_address);
static bool swap_out_one_locked(void);
static void space_register(uint64_t address_space);
static void space_unregister(uint64_t address_space);
#define SWAP_ENTRY_KEEP (VMM_PAGE_WRITABLE | VMM_PAGE_USER | VMM_PAGE_NX | \
                         VMM_PAGE_COW | VMM_PAGE_ANON)
#define SWAP_SLOT_OF(entry) ((uint32_t)(((entry) & PAGE_ADDRESS_MASK) >> 12))
#define VMM_PAGE_ACCESSED ARCH_PTE_ACCESSED
#define VMM_SPACE_MAX 1024
#define USER_TOP ARCH_MMU_USER_TOP

static uint64_t kernel_address_space;

static void tlb_flush_space(uint64_t address_space, uint64_t virtual_address)
{
  if (!address_space || address_space == kernel_address_space ||
      virtual_address >= 0xFFFF800000000000ULL)
    smp_tlb_shootdown();
  else
    smp_tlb_shootdown_space(address_space);
}
static bool nx_enabled;
#define CR0_WP (1ULL << 16)
void vmm_init(void)
{
  kernel_address_space = arch_mmu_read_root();
  nx_enabled = arch_mmu_enable_nx();
  arch_mmu_enable_write_protect();
  klogf(KLOG_OK, "vmm: kernel cr3=0x%llx nx=%s", kernel_address_space,
        nx_enabled ? "on" : "unsupported");
}

bool vmm_init_cpu(void)
{
  if (!kernel_address_space)
    return false;
  if (nx_enabled && !arch_mmu_enable_nx())
    return false;
  arch_mmu_enable_write_protect();
  vmm_switch_address_space(kernel_address_space);
  return true;
}

uint64_t vmm_kernel_address_space(void) { return kernel_address_space; }
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
    arch_tlb_flush_page(virtual_address);
    return true;
  }
  uint64_t copy = pmm_allocate_page();
  if (!copy)
    return false;
  memcpy(pmm_physical_to_virtual(copy), pmm_physical_to_virtual(frame),
         PMM_PAGE_SIZE);
  *slot = (entry & ~(PAGE_ADDRESS_MASK | VMM_PAGE_COW)) | copy |
          VMM_PAGE_WRITABLE;
  arch_tlb_flush_page(virtual_address);
  pmm_free_page(frame);
  return true;
}
static bool leaf_clone(uint64_t *source, uint64_t *destination)
{
  uint64_t entry = *source;
  if (!(entry & VMM_PAGE_PRESENT))
  {
    if (entry & VMM_PAGE_SWAPENT)
    {
      if (!swap_dup(SWAP_SLOT_OF(entry)))
        return false;
      *destination = entry;
    }
    return true;
  }
  uint64_t frame = entry & PAGE_ADDRESS_MASK;
  if (entry & VMM_PAGE_DEVICE)
  {
    *destination = entry;
    return true;
  }
  if (!pmm_frame_ref(frame))
    return false;
  if (!(entry & VMM_PAGE_SHARED) &&
      (entry & (VMM_PAGE_WRITABLE | VMM_PAGE_USER)) ==
          (VMM_PAGE_WRITABLE | VMM_PAGE_USER))
  {
    entry = (entry & ~VMM_PAGE_WRITABLE) | VMM_PAGE_COW;
    *source = entry;
  }
  *destination = entry;
  return true;
}

static void leaf_release(uint64_t entry)
{
  if (entry & VMM_PAGE_PRESENT)
  {
    if (!(entry & VMM_PAGE_DEVICE))
      pmm_free_page(entry & PAGE_ADDRESS_MASK);
  }
  else if (entry & VMM_PAGE_SWAPENT)
    swap_free(SWAP_SLOT_OF(entry));
}

static const struct arch_mmu_leaf_ops leaf_ops = {leaf_clone, leaf_release};

static uint64_t *user_slot_locked(uint64_t address_space,
                                  uint64_t virtual_address)
{
  return arch_mmu_walk(address_space, virtual_address, false, 0);
}

static uint64_t *user_slot_skip_locked(uint64_t address_space,
                                       uint64_t virtual_address,
                                       uint64_t *next)
{
  return arch_mmu_walk_skip(address_space, virtual_address, next);
}

static uint64_t vmm_user_page_count_locked(uint64_t address_space)
{
  if (!address_space || address_space == kernel_address_space)
    return 0;
  return arch_mmu_count_user_pages(address_space);
}

static uint64_t vmm_create_address_space_locked(void)
{
  uint64_t physical = arch_mmu_root_new(kernel_address_space);
  if (!physical)
    return 0;
  space_register(physical);
  return physical;
}

static void vmm_destroy_address_space_locked(uint64_t address_space)
{
  if (!address_space || address_space == kernel_address_space)
    return;
  space_unregister(address_space);
  struct arch_mmu_detached detached;
  arch_mmu_detach_user(address_space, &detached);
  tlb_flush_space(address_space, 0); /* Acknowledge before returning any frame to PMM. */
  arch_mmu_free_detached(&detached, &leaf_ops);
  pmm_free_page(address_space);
}

static bool vmm_map_page_locked(uint64_t address_space,
                                uint64_t virtual_address,
                                uint64_t physical_address, uint64_t flags)
{
  if ((virtual_address & (PMM_PAGE_SIZE - 1)) ||
      (physical_address & (PMM_PAGE_SIZE - 1)))
    return false;
  uint64_t *slot = arch_mmu_walk(address_space, virtual_address, true, flags);
  if (!slot)
    return false;
  if (*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT))
    return false;
  *slot = (physical_address & PAGE_ADDRESS_MASK) | VMM_PAGE_PRESENT |
          (flags & (VMM_PAGE_WRITABLE | VMM_PAGE_USER | VMM_PAGE_ANON |
                    VMM_PAGE_SHARED | VMM_PAGE_DEVICE)) |
          arch_mmu_nx_bits(flags);
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
  uint64_t *slot = arch_mmu_walk(address_space, virtual_address, false, 0);
  if (!slot)
    return false;
  if ((*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)) == VMM_PAGE_SWAPENT &&
      !swap_in_locked(slot, virtual_address))
    return false;
  uint64_t entry = *slot;
  if (!(entry & VMM_PAGE_PRESENT))
    return false;
  *slot = (entry & PAGE_ADDRESS_MASK) | VMM_PAGE_PRESENT |
          (entry & (VMM_PAGE_ANON | VMM_PAGE_SHARED)) |
          (flags & (VMM_PAGE_WRITABLE | VMM_PAGE_USER)) |
          arch_mmu_nx_bits(flags);
  return true;
}

static uint64_t vmm_translate_locked(uint64_t address_space,
                                     uint64_t virtual_address)
{
  uint64_t *slot = arch_mmu_walk(address_space, virtual_address, false, 0);
  if (!slot)
    return 0;
  if ((*slot & (VMM_PAGE_PRESENT | VMM_PAGE_SWAPENT)) == VMM_PAGE_SWAPENT &&
      !swap_in_locked(slot, virtual_address & ~(PMM_PAGE_SIZE - 1)))
    return 0;
  uint64_t entry = *slot;
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
  for (uint64_t page = first;; page += PMM_PAGE_SIZE)
  {
    uint64_t *slot = arch_mmu_walk(address_space, page, false, 0);
    if (!slot)
      return false;
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
void vmm_switch_address_space(uint64_t address_space)
{
  if (!address_space || arch_mmu_read_root() == address_space)
    return;
  smp_note_cr3(address_space);
  arch_mmu_activate(address_space);
}

bool vmm_protect_page(uint64_t address_space, uint64_t virtual_address,
                      uint64_t flags)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  bool result = vmm_protect_page_locked(address_space, virtual_address, flags);
  tlb_flush_space(address_space, virtual_address);
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
  tlb_flush_space(address_space, virtual_address);
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return result;
}

bool vmm_map_new_pages(uint64_t address_space, uint64_t virtual_address,
                       uint64_t page_count, uint64_t flags)
{
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  bool result = vmm_map_new_pages_locked(address_space, virtual_address,
                                         page_count, flags);
  tlb_flush_space(address_space, virtual_address);
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
    bool ok = arch_mmu_clone_user(source, root, &leaf_ops);
    if (ok)
      result = root;
    else
      vmm_destroy_address_space_locked(root);
  }
  tlb_flush_space(source, 0);
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

int vmm_map_shared_page(uint64_t address_space, uint64_t virtual_address,
                        uint64_t physical_address, uint64_t flags)
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
  bool ok = vmm_map_page_locked(address_space, virtual_address, physical_address,
                                (flags & ~VMM_PAGE_ANON) | VMM_PAGE_SHARED);
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return ok ? 1 : -1;
}

bool vmm_map_device_range(uint64_t address_space, uint64_t virtual_address,
                          uint64_t physical_address, uint64_t page_count,
                          uint64_t flags)
{
  if (!address_space || address_space == kernel_address_space ||
      (virtual_address & (PMM_PAGE_SIZE - 1)) ||
      (physical_address & (PMM_PAGE_SIZE - 1)) ||
      !range_valid(address_space, virtual_address, page_count))
    return false;
  uint64_t irq_flags = spin_lock_irqsave(&vmm_lock);
  bool ok = true;
  uint64_t done = 0;
  for (; done < page_count; done++)
  {
    uint64_t va = virtual_address + done * PMM_PAGE_SIZE;
    if (!vmm_map_page_locked(address_space, va,
                             physical_address + done * PMM_PAGE_SIZE,
                             (flags & ~(VMM_PAGE_ANON | VMM_PAGE_SHARED)) |
                                 VMM_PAGE_DEVICE | VMM_PAGE_USER))
    {
      ok = false;
      break;
    }
  }
  if (!ok)
    for (uint64_t i = 0; i < done; i++)
    {
      uint64_t *slot = user_slot_locked(address_space,
                                        virtual_address + i * PMM_PAGE_SIZE);
      if (slot)
        *slot = 0;
      arch_tlb_flush_page(virtual_address + i * PMM_PAGE_SIZE);
    }
  spin_unlock_irqrestore(&vmm_lock, irq_flags);
  return ok;
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
static void vmm_page_prot_locked(uint64_t *slot, uint64_t flags)
{
  uint64_t entry = *slot;
  uint64_t base = entry & ~(VMM_PAGE_WRITABLE | VMM_PAGE_USER | VMM_PAGE_NX);
  if (flags & VMM_PAGE_USER)
    base |= VMM_PAGE_USER;
  if (arch_mmu_nx_bits(flags))
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
      bool device = (*slot & VMM_PAGE_DEVICE) != 0;
      if (!device)
        frames[held++] = *slot & PAGE_ADDRESS_MASK;
      *slot = 0;
      arch_tlb_flush_page(address);
      if (!device)
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
      tlb_flush_space(address_space, virtual_address);
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
      arch_tlb_flush_page(address);
    }
    else if (populate)
    {
      ok = vmm_map_new_pages_locked(address_space, address, 1, flags);
      next = address + PMM_PAGE_SIZE;
    }
    address = next;
  }
  tlb_flush_space(address_space, virtual_address);
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
    arch_tlb_flush_page(address);
    tlb_flush_space(address_space, address);
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
