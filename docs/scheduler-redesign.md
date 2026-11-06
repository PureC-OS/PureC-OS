# Scheduler and process model redesign

This document records the requirements and the staged plan for replacing the
current scheduler (`src/kernel/process/scheduler.c`) and process model
(`src/kernel/process/process.c`). The work is done in stages; the system must
boot and keep running the existing scenarios (desktop, terminal, Vim, TinyCC,
Make, Doom) after every stage.

## Why

Three kernel panics in a row (`unbalanced preempt enable` twice and
`mutex: invalid owner`) came from one class of defect: per-CPU state was read
with interrupts enabled, so a thread could migrate to another CPU between
finding "its" CPU and using the result. Per-CPU data reached through `%gs`
removes that class by construction.

## Requirements

- Linux-like observable model: load average (1/5/15 min), uptime, task counts
  (running, sleeping, stopped, zombie, threads) and per-task CPU and memory
  usage, in the form shown by `top`, `htop` and `btop`.
- POSIX process model: `fork`, `execve`, `wait*`, process groups, sessions,
  controlling terminal, job control (`SIGTSTP`, `SIGCONT`, `SIGTTIN`,
  `SIGTTOU`, `SIGCHLD`), background processes, sub-processes, `nice` and
  `setpriority`.
- A task is the unit of scheduling. A process is a group of tasks sharing an
  address space and a descriptor table. Kernel threads are tasks without a
  user address space.
- PID 1 (`init`) is protected. It cannot be killed by signals from user space.
  Only the kernel ends it, as a controlled shutdown, as today. It gets the
  highest scheduling class.
- Sockets and a reworked system call layer are part of the later stages.
- Nothing that works today may break. The public kernel interfaces
  (`scheduler.h`, `process.h`) and the system call ABI stay stable until a
  stage explicitly replaces them with a documented compatible version.

## Stages

1. Per-CPU area through `%gs` with correct `swapgs` handling (`percpu.md`).
   `current` thread is published there and read with one instruction.
2. Per-CPU preemption count and the remaining per-CPU scheduler state moved
   into the per-CPU area. Debug checks: blocking while preemption is disabled,
   spinlock owner tracking.
3. Wait queues replacing the polling loops built on `scheduler_sleep(1)`
   (tty, pipe, `poll`, mutex, `waitpid`).
4. Split `struct thread` into task and process. Parent/child links, zombies,
   re-parenting to `init`, `SIGCHLD`.
5. Process groups, sessions, controlling terminal, job control, `nice` and
   scheduling classes. Load average and task accounting for monitors.
6. Per-CPU run queues with load balancing.
7. System call layer rework and sockets.

## Verification

Host tests in `tests/` (`scheduler_cpu_test`, `scheduler_balance_test`) must
pass after every stage. Logic that does not need hardware is kept testable on
the host. Hardware behaviour is verified in the VM with a memory dump taken on
any panic.
