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

## Run queues and balancing (implemented, stage 6 part 1)

Every CPU owns a run queue (`rq_*` fields of `struct scheduler_cpu`) holding the
threads that are ready and placed on it. The running thread is not in the
queue. All queues, the sleeper list and the thread list are still protected by
the single `runqueue_lock`; splitting it per queue is the next step. The lock is
still held across the context switch and released in `finish_switch()`, so no
other CPU can pick a thread whose stack is still in use.

- Placement (`select_cpu`): among started CPUs the thread may run on, the least
  loaded one wins; the CPU it last ran on is kept unless it is loaded by at
  least one thread weight more than the best choice. `kernel_only` threads
  (user threads inside a system call) stay on CPU 0, pinned threads on their
  CPU. Placement happens on create, wake-up and when a preempted thread is put
  back.
- Selection (`rq_select`): the earlier EEVDF-like rule, now over the local queue
  only, so it costs O(local queue) instead of O(all threads). Time slices are
  computed from the weight of the local queue.
- Idle pull (`pull_task(id, true)`): a CPU with nothing to run takes one
  thread from the most loaded queue. Periodic pull (every 4 ms on each CPU,
  skipping threads that ran within the last 2 ms). Both move a thread only when
  the source load exceeds the destination load by more than the thread weight,
  which strictly reduces the imbalance and prevents ping-pong. A thread stranded
  in a queue of a CPU it may not run on is always taken.
- `virtual_runtime` is kept relative to the queue minimum while a thread is
  blocked or moves between queues, so a migrated or woken thread neither starves
  nor overtakes the destination queue.
- Sleepers wait in one list ordered by wake-up time. Expired sleepers are woken
  on every timer tick of any CPU and placed with the same rules.
- A thread placed on another CPU makes the placing CPU send a reschedule IPI to
  that CPU only (previously every CPU was interrupted).

Known limits: one global lock; no wake-up preemption (a woken thread placed on
a busy CPU waits for that CPU's slice to end unless an idle CPU pulls it);
system calls of user threads still run on CPU 0.

## Verification

Host tests in `tests/` (`scheduler_cpu_test`, `scheduler_balance_test`) must
pass after every stage. Logic that does not need hardware is kept testable on
the host. Hardware behaviour is verified in the VM with a memory dump taken on
any panic.
