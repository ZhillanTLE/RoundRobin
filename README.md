### Round Robin

**Group:**
1. Zhillan Baniaksa, 2506637174
2. Maglio Razzy E., 2506553616
**Class:** KKI

All numbers below come from our captured run in `sample_output.txt` (default quantum `-q 2` unless stated otherwise).
## 1. How our algorithm decides

Round Robin gives each process a fixed slice of CPU time, the **time quantum** (`TIME_QUANTUM`, set with `-q`, default 2). `choose_next()` is called once per time unit and makes the decision in two steps:

1. **Keep the CPU.** If a process is running, still has work (`remaining > 0`), and has used fewer than `TIME_QUANTUM` consecutive ticks, it keeps the CPU. We count that tick (`ticks_used++`).
2. **Rotate.** Otherwise we scan forward through the process list, starting just after the last process that ran (`last_idx`) and wrapping around with `% sim->n`. The first process found that has arrived (`arrival <= clock`) and is unfinished (`remaining > 0`) gets the CPU, and the counter is reset to 1.

If no process is ready, we return `NULL` and the CPU idles for that tick. This happens in `tc2`, at t=4–6 and t=11–14.

Design details:

- **State between calls.** `ticks_used` and `last_idx` are `static int` locals, so they keep their values from one tick to the next.
- **Rotation after a finish.** We scan from `last_idx`, not from the start of the list. When a process finishes, the next one to run is therefore the one _after_ it in the queue, not P1 again.
- **Counter reset.** The counter resets every time the CPU goes to a process, including when the previous process finished early. It does not only reset when a quantum expires.
- **Ties.** The scan visits processes in PID order, so when several are ready the lowest PID after the current position wins. On the first dispatch the scan starts at index 0. `tc5` (all arrive at t=0) gives identical output on every run.
- **We never decrement `remaining`.** The process thread does that inside `run_one_tick()`.
- **The quantum sweep (`rr_replay()`).** This function replays the same workload at q = 1, 2, 4, 8 with the same rules, but without threads. That lets `print_topic_extra()` print the whole comparison from a single run.

### Process state transitions

Each process moves through the lecture's state diagram. From `./scheduler tests/tc1.txt -v`:

```
t=0   P1  NEW     -> READY        (arrives)
t=0   P1  READY   -> RUNNING      (dispatched)
t=2   P1  RUNNING -> READY        (quantum of 2 expired: preempted)
t=6   P3  RUNNING -> TERMINATED   (burst finished inside its quantum)
```

The `RUNNING -> READY` transition is what makes Round Robin preemptive. FCFS never produces it.

## 2. Why the simulation is repeatable even though it uses real threads

Every process is a real POSIX thread, and all of them share one `Sim` struct. They do not race, because of a semaphore handshake:

```c
/* scheduler side (run_one_tick)      process thread side */
sem_post(&p->run_sem);           //   sem_wait(&p->run_sem);
sem_wait(&sim->tick_done);       //   p->remaining--;
                                 //   sem_post(&sim->tick_done);
```

Every process thread spends its life blocked on its own `run_sem`. The scheduler posts **exactly one** process's semaphore and then blocks on `tick_done` until that process reports back. At any instant, therefore, at most one thread is runnable. The OS cannot interleave two process threads, because only one of them is ever allowed to run.

The _order_ in which threads run is decided entirely by our deterministic `choose_next()`, not by the OS thread scheduler. Ties are broken by lowest PID, and the same input always produces the same Gantt chart. We checked this by running `tc5` twice and comparing the outputs.

## 3. What `fork_vs_thread.c` showed

**Part 1: two threads, one counter.** Two threads each run `counter++` 100,000 times with no lock. Threads share the process's memory, so both update the _same_ variable. `counter++` is really three steps (load, add, store), so if the two threads interleave, increments are lost and the total falls below 200,000.

In our captured run the result was exactly 200,000 ("no race observed"). The reason is that the Makefile compiles with `-O2`, and the compiler turns the loop into a single addition, so the window for a collision is tiny. Built with `-O0`, the loop does a real load/add/store every iteration, and the result usually drops below 200,000. A race does not show itself on every run, and that is exactly what makes races dangerous.

**Part 2: a forked process.** The child sets `counter = 999` and prints it. The parent `wait()`s for the child and then prints its own `counter`:

```
child  (pid 3069): counter = 999
parent (pid 3066): counter = 0
```

`fork()` gives the child its **own copy** of the address space (copy-on-write), so the child's write never reaches the parent.

**Why threads need semaphores and separate processes do not:** separate processes have separate memory, so there is no shared variable to race on. Threads share one address space. In our scheduler every process thread can read and write the same `Sim` struct (`remaining`, `gantt[]`, the counters). Without the semaphore handshake they would all run at once, corrupt that shared state, and give a different answer on every run.

## 4. Trade-off, with our numbers

**The quantum trades response time against context-switch overhead.** On `tc1`:

|Quantum|avg WT|avg TAT|avg RT|Context switches|
|---|---|---|---|---|
|1|9.40|13.80|**0.00**|**19**|
|2|9.40|13.80|2.00|11|
|4|9.20|13.60|5.20|6|
|8|8.60|13.00|8.60|4|
|_FCFS_|_8.60_|_13.00_|_8.60_|_4_|

At quantum 1 the average response time was **0.00**, because every process got the CPU in the very first round. That run needed **19** context switches. At quantum 8 nothing was ever preempted, because every burst in `tc1` is 8 or less, so each process finished inside its first slice. Round Robin then **degenerated into FCFS**: avgWT 8.60, avgRT 8.60 and 4 switches, identical to the FCFS baseline.

A real switch costs time (saving and restoring registers, cache and TLB effects). Going from q=2 to q=1 nearly doubles the switches (11 → 19) to save 2 time units of average response time.

**Our choice for an interactive system: q = 2.** It keeps response time low (2.00 on `tc1`, against 8.60 for FCFS), and it needs 8 fewer context switches than q=1. A larger quantum (4, 8) pushes response time back toward FCFS levels. This matches lecture slides 5.23–5.24: the quantum should be large compared with the cost of a context switch, but not so large that RR turns into FCFS.

### Comparison against FCFS, all five test cases (q = 2)

|Test|FCFS avgWT|RR avgWT|FCFS avgRT|RR avgRT|Switches FCFS → RR|
|---|---|---|---|---|---|
|tc1|8.60|9.40 (+9.3%)|8.60|2.00 (−76.7%)|4 → 11|
|tc2|1.20|1.00 (−16.7%)|1.20|0.40 (−66.7%)|4 → 6|
|tc3|19.40|10.40 (−46.4%)|19.40|2.00 (−89.7%)|4 → 20|
|tc4|15.70|18.20 (+15.9%)|15.70|4.20 (−73.2%)|9 → 22|
|tc5|7.40|9.20 (+24.3%)|7.40|3.80 (−48.6%)|4 → 8|

**What RR does better.** Response time improves on _every_ test case, by between 48.6% and 89.7%. The mechanism is preemption. No process can hold the CPU for longer than one quantum, so every newly arrived process waits at most about (n−1)·q before it first runs. In FCFS, a process waits for every earlier burst to finish completely.

**What it costs.** First, context switches go up on every test, by between +50% and +400%. Second, **average waiting time gets worse when bursts are similar in length**: tc1 +9.3%, tc4 +15.9%, tc5 +24.3%. Under RR, jobs of similar length interleave, and each one finishes late. In `tc5`, for example, P1–P3 (burst 4 each) all finish between t=11 and t=15. Under FCFS, P1 finishes at t=4 and P2 at t=8. RR also needs a well-chosen quantum: too small wastes time on switches, too large turns it into FCFS.

**Where the difference is largest: `tc3`** (bursts 20, 2, 3, 15, 1). Under FCFS, P1's 20-unit burst runs first, and P2, P3 and P5, all short, are stuck behind it (and then behind P4's 15). This is the **convoy effect**. RR breaks the convoy. P2 finishes at t=4 instead of t=22, and P5 at t=9 instead of t=41. Average waiting time drops from 19.40 to 10.40 (−46.4%) and response time from 19.40 to 2.00 (−89.7%).

The property that causes this is **highly uneven burst lengths with long jobs arriving first**. In contrast, `tc2` shows the _smallest_ difference: its jobs are short and arrive spread out, with idle gaps between them, so there is rarely a queue to reorder. When every burst fits within the quantum (tc1 at q=8, tc5 at q≥4, which give the same numbers as FCFS), RR and FCFS are identical.


## 5. Manual verification (deterministic modelling, slide 5.74)

**Test case:** `tc1.txt`, quantum = 2.

|PID|AT|BT|
|---|---|---|
|P1|0|8|
|P2|1|4|
|P3|2|2|
|P4|3|5|
|P5|4|3|

**Working the schedule by hand.** The table lists the remaining burst of each process _after_ each slice.

|Time|Runs|Why|Remaining after slice|
|---|---|---|---|
|0–2|P1|only P1 has arrived|P1=6|
|2–4|P2|quantum expired; next after P1 is P2|P2=2|
|4–6|P3|next after P2|P3=0 → **P3 done at 6**|
|6–8|P4|next after P3|P4=3|
|8–10|P5|next after P4|P5=1|
|10–12|P1|wrap around to P1|P1=4|
|12–14|P2|next|P2=0 → **P2 done at 14**|
|14–16|P4|P3 finished, skip it|P4=1|
|16–17|P5|only 1 left|P5=0 → **P5 done at 17**|
|17–19|P1|next after P5 (wrap)|P1=2|
|19–20|P4|P2, P3 finished, skip|P4=0 → **P4 done at 20**|
|20–22|P1|only one left|P1=0 → **P1 done at 22**|

```
| P1 | P2 | P3 | P4 | P5 | P1 | P2 | P4 | P5 | P1 | P4 | P1 |
0    2    4    6    8    10   12   14   16   17   19   20   22
```

Context switches: there are 12 slices, and every boundary between them is a change of process, so there are 11 switches.

**Process P3**

```
Arrival Time    : 2
Burst Time      : 2
First Start     : 4
Completion Time : 6
TAT = CT - AT = 6 - 2 = 4
WT  = TAT - BT = 4 - 2 = 2
RT  = First Start - AT = 4 - 2 = 2
```

**Process P5**

```
Arrival Time    : 4
Burst Time      : 3
First Start     : 8
Completion Time : 17
TAT = CT - AT = 17 - 4 = 13
WT  = TAT - BT = 13 - 3 = 10
RT  = First Start - AT = 8 - 4 = 4
```

**Process P1**

```
Arrival Time    : 0
Burst Time      : 8
First Start     : 0
Completion Time : 22
TAT = CT - AT = 22 - 0 = 22
WT  = TAT - BT = 22 - 8 = 14
RT  = First Start - AT = 0 - 0 = 0
```

**Comparison with the program** (`sample_output.txt`, tc1 scheduling table):

|PID|CT (hand / program)|TAT|WT|RT|
|---|---|---|---|---|
|P1|22 / 22|22 / 22|14 / 14|0 / 0|
|P3|6 / 6|4 / 4|2 / 2|2 / 2|
|P5|17 / 17|13 / 13|10 / 10|4 / 4|

Context switches: 11 by hand, 11 in the program. **The hand calculation matches the program exactly.**

We also checked the starter's FCFS baseline against the published lecture answer for `tc1`. The FCFS column in our comparison table (avgWT 8.60) agrees with P1 WT=0, P2 WT=7, P3 WT=10, P4 WT=11, which together with P5 WT=15 gives (0+7+10+11+15)/5 = 8.60.

## 6. Who wrote which part

| Part                                                         | Member                               | NPM                    |
| ------------------------------------------------------------ | ------------------------------------ | ---------------------- |
| `choose_next()`: Round Robin decision logic                  | Maglio Razzy E.                      | 2506553616             |
| `rr_replay()` and the quantum sweep in `print_topic_extra()` | Maglio Razzy E. and Zhillan Baniaksa | 2506553616, 2506637174 |
| `fork_vs_thread.c`, Part 2                                   | Zhillan Baniaksa                     | 2506637174             |
| Test runs, `sample_output.txt`, `scheduling_report.csv`      | Zhillan Baniaksa                     | 2506637174             |
| Manual verification (section 5)                              | Maglio Razzy E. and Zhillan Baniaksa | 2506553616, 2506637174 |
| FCFS analysis and trade-off (section 4)                      | Zhillan Baniaksa                     | 2506637174             |
| `explanation.md` and `presentation.pptx`                     | Maglio Razzy E. and Zhillan Baniaksa | 2506553616, 2506637174 |
