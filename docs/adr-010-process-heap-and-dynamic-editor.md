# ADR-010: Per-process heap and dynamic editor buffer

## Status

Accepted

## Context

The ring-3 nano editor stored the complete file in a 4096-byte static array.
Files larger than that were rejected even when physical memory and virtual
address space were available. The userspace runtime had no allocation
primitive, so merely increasing the array would replace one artificial limit
with another and enlarge every nano image.

Each process already owns an address space containing its ELF segments and a
fixed stack near the top of the user range. Destruction of that address space
releases every mapped user page.

## Decision

Give every ring-3 process a grow-only heap. It begins one unmapped guard page
after the loaded ELF image and ends one guard page before the user stack.
`SYS_HEAP_GROW` advances the logical break and maps zeroed pages on demand as
user-writable and non-executable. The syscall returns the previous break so
contiguous storage can be built without exposing page-table details to an
application. `pc_heap_grow()` provides the corresponding libc boundary.

Map pages individually and retain an exact mapped-end marker. If physical
allocation fails partway through a request, the logical break does not move;
the successfully mapped pages remain tracked and can satisfy a later retry.

Nano starts with a 4 KiB allocation and doubles its buffer only when loading or
typing requires more capacity. The 4 KiB value is therefore an initial
allocation, not a file-size limit. The remaining maximum is the available
process memory and the existing 32-bit file-size ABI.

## Consequences

### Positive

- Nano can open and save files larger than 4 KiB without reserving a large
  static region in every instance.
- Heap pages are isolated by the process CR3, non-executable and automatically
  reclaimed when the process is reaped.
- Other ring-3 programs can use the same small allocation primitive.
- Large editor contents are emitted with one buffered write instead of one
  syscall per character.

### Negative

- The first heap primitive only grows; it cannot shrink or free individual
  allocations during the process lifetime.
- Nano's file length remains bounded by the current `uint32_t` VFS read/write
  interface.
- A general allocator will need metadata and reuse rules above
  `pc_heap_grow()` if long-lived applications begin making many allocations.

## Alternatives Considered

**Increase the static editor array** was rejected because it preserves an
arbitrary ceiling and permanently increases the ELF memory footprint.

**Place the file on the user stack** was rejected because the stack is fixed at
16 pages and must remain available for control flow and local variables.

**Add a complete `malloc` implementation now** was deferred because nano needs
one monotonically growing buffer and its entire address space is reclaimed at
exit. The syscall provides a stable base for a future allocator.

## References

- `src/kernel/process.c`
- `src/kernel/syscall.c`
- `src/libc/runtime.c`
- `src/programs/nano/editor.c`
- `docs/adr-005-standalone-terminal-nano-environment.md`
