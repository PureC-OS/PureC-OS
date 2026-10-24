# ADR-003: Ring-3 Processes and a Separate Installer ELF

## Status
Superseded by ADR-004

## Context
The scheduler previously switched only linked ring-0 threads. Every thread
shared the bootloader page tables, `GETPID` returned a constant, `EXIT` halted
the CPU, file descriptors were global, and the installer object was linked into
the kernel image. Calling this userspace did not provide isolation.

## Decision
Introduce a physical page allocator over Limine usable-memory entries and an
x86_64 virtual-memory manager. Each ELF64 process receives a private PML4,
user-mapped load segments, a non-executable user stack where supported, a PID,
an exit state and a descriptor table. Higher-half kernel mappings are shared
but retain supervisor-only permissions.

Extend the scheduler with an address space, owner process and ring mode per
thread. Update CR3 and TSS.RSP0 at context switches. Enter programs through the
ring-3 GDT selectors and `iretq`; timer interrupts and system calls return
through the existing interrupt frame.

Compile the installer with its own linker script, omit its object from
`kernel-limine.elf`, and copy it as a separate boot module. ADR-004 standardizes
the final path as `/bin/installer`.
Limine supplies it as a boot module. The shell starts it with `SYS_EXEC` and
waits with `SYS_WAIT`. Only this trusted module receives permission to invoke
destructive formatting calls.

The standalone installer exposes only the verified UEFI/GPT path. The previous
BIOS prompt wrote a FAT volume without installing a complete BIOS boot chain,
so presenting that path as bootable was unsafe.

FAT32 and device drivers remain in the kernel during this stage. Their public
boundary is the syscall ABI; moving VFS policy to a filesystem server requires
IPC and is a later decision.

## Consequences

### Positive
- Installer failure is isolated from kernel control flow and address space.
- PID, exit, wait, preemption and per-process descriptors have real semantics.
- Invalid user buffers are rejected before kernel subsystems dereference them.
- Exited, waited-for processes release user frames and page-table frames.

### Negative
- The PMM currently manages at most 32 GiB to keep its static bitmap bounded.
- There is one user thread per process and no fork, signals or shared memory.
- Unwaited children remain zombies and retain their address spaces.
- VFS serialization is still a single global lock.

## Alternatives Considered
- Keep linked ring-0 programs: rejected because it provides no fault isolation.
- Move FAT32 immediately behind ad-hoc callbacks in a fake process: rejected
  because callbacks would retain kernel privilege without real IPC isolation.
- Load the installer from the mounted root filesystem: deferred because the
  installer must run from read-only boot media before a target disk exists.

## Failure Modes
- Invalid ELF metadata or an allocation failure rejects `SYS_EXEC`.
- A user exception terminates the calling process instead of panicking the OS.
- A missing installer module makes the shell command fail without changing disk.
- Formatting calls without the storage capability return an error.
