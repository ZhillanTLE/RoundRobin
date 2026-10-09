# Starter Kit: Programming Assignment 2

This is given to you. Copy the whole folder, then edit **`scheduler.c`** and
**`fork_vs_thread.c`**. You should not need to change anything else.

## Build and run

```bash
make                            # builds ./scheduler and ./fork_vs_thread
./scheduler tests/tc1.txt       # run one workload
./scheduler tests/tc1.txt -v    # also print every process state transition
./scheduler tests/tc5.txt -q 4  # set the time quantum (Round Robin / MLFQ)
make test                       # run all five test cases
make clean
```

**Out of the box the scheduler runs FCFS**, which is given to you complete as a
worked example in `scheduler.c`. Run it, read it, and check its numbers against
the published answer below. Then replace the body of `choose_next()` with your
own algorithm.

FCFS is also the **baseline every topic is compared against**. One line in
`print_topic_extra()`,

```c
print_vs_fcfs(sim, ALGORITHM_NAME);
```

produces the whole comparison table. `fcfs_baseline()` in `simlib.c` replays
the same workload under FCFS for you, so you never have to write a second
scheduler.

## What is in the kit

| File | Yours? | What it is |
|---|---|---|
| `scheduler.c` | **edit this** | `fcfs_choose()` (given, the worked example) and `choose_next()` (yours) |
| `fork_vs_thread.c` | **edit this** | A short `fork()` vs threads demonstration |
| `pcb.h` | read it | The Process Control Block and the shared `Sim` state |
| `simlib.c` / `simlib.h` | read it | Workload loading, the threads, the printing |
| `Makefile` | given | Build rules |
| `tests/tc1..tc5.txt` | given | The five required test cases |

## How the simulation works

Time advances one unit at a time. At each unit the main loop asks your
`choose_next()` which process should get the CPU, then calls `run_one_tick()`.

Each process is a **real POSIX thread**, created in `start_processes()`. A
process thread spends its whole life blocked on its own semaphore:

```c
for (;;) {
    sem_wait(&p->run_sem);        /* wait until the scheduler picks me */
    if (p->remaining <= 0) break;
    p->remaining--;               /* use the CPU for one time unit */
    sem_post(&p->owner->tick_done);   /* tell the scheduler I am done */
}
```

and the dispatcher is the other half of that handshake:

```c
sem_post(&p->run_sem);            /* you may run for one unit */
sem_wait(&sim->tick_done);        /* block until that unit is over */
```

**This is why the output is repeatable.** The threads are real and they all
share one `Sim` struct, but the scheduler releases exactly one semaphore and
then blocks, so at most one process thread is runnable at any instant. That is
mutual exclusion, built from the semaphores in the W04 lecture, and it is the
same pattern as `sem-thread.c` in the examples you were given.

Take the semaphores away and the threads would all decrement `remaining` and
write to `gantt[]` at once: a race condition, and a different answer every run.
`fork_vs_thread.c` shows you that race directly.

## Workload file format

One process per line: `pid arrival burst priority`. Lines beginning with `#`
are comments. Priority is optional and defaults to 0; **a smaller priority
number means more urgent.**

```
# pid arrival burst priority
1   0   8   3
2   1   4   1
```

## The five test cases

| File | What it tests |
|---|---|
| `tc1.txt` | Normal conditions, 5 processes |
| `tc2.txt` | Different arrival times, including a gap where the CPU must idle |
| `tc3.txt` | Very uneven burst times (20, 2, 3, 15, 1): the convoy effect |
| `tc4.txt` | Many processes (10) |
| `tc5.txt` | Edge cases: identical arrivals, repeated bursts, tied priorities |

`tc1.txt` is the workload from the lecture handout, so you can check your
FCFS numbers against a published answer:

```
P1 CT=8  TAT=8  WT=0  RT=0
P2 CT=12 TAT=11 WT=7  RT=7
P3 CT=14 TAT=12 WT=10 RT=10
P4 CT=19 TAT=16 WT=11 RT=11
```

## Ties

Whenever two processes are equally good, **pick the lower PID.** Every test
case is built so that a consistent tie-break gives one right answer. If you
break ties differently your Gantt chart will not match the answer key, and you
will not be able to reproduce your own results.

## A warning about `remaining`

`run_one_tick()` decrements `p->remaining` *inside the process thread*. Do not
decrement it yourself in `choose_next()`, or every process will finish in half
the time and every number in your report will be wrong.
