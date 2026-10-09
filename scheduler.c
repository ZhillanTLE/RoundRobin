/*
 * scheduler.c -- THIS IS THE FILE YOU EDIT.
 *
 * Programming Assignment 2, CSCM602055 Operating Systems.
 *
 * Everything else (loading the workload, creating the threads, the semaphore
 * handshake, the printing) is already written for you in simlib.c. Your job is
 * the scheduling decision itself, and the extra output your topic asks for.
 *
 * Build and run:
 *     make
 *     ./scheduler tests/tc1.txt
 *     ./scheduler tests/tc1.txt -v          (show every state transition)
 *
 * HOW THE SIMULATION WORKS
 * ------------------------
 * Time advances one unit at a time. At each time unit the main loop asks your
 * choose_next() which process should get the CPU. It then calls run_one_tick(),
 * which posts that process's semaphore, lets its thread run for one unit, and
 * waits for it to finish. Exactly one process thread is runnable at any moment,
 * which is why the output is repeatable.
 *
 *     for each time unit t:
 *         admit any process whose arrival time is t   (NEW -> READY)
 *         p = choose_next(...)                        <-- YOUR CODE
 *         if p is NULL: run_idle_tick()
 *         else:         run_one_tick(sim, p)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simlib.h"

/* Set this to your algorithm's name; it appears in the report header. */
static const char *ALGORITHM_NAME = "TODO: your algorithm";

/* Round Robin / MLFQ only. Ignore it for the other algorithms. */
static int TIME_QUANTUM = 2;

/* ===================================================================== */
/*  THE BASELINE: FCFS, fully implemented as a worked example             */
/* ===================================================================== */
/*
 * This is First Come First Served, and it is GIVEN to you complete.
 *
 * Read it. It is the shape your own algorithm will take, and it is also the
 * baseline every topic is compared against: `print_vs_fcfs()` in simlib.c
 * works out what FCFS would have done on the same workload, so you never have
 * to write a second scheduler to make the comparison.
 *
 * FCFS is non-preemptive: once a process has the CPU it keeps it until its
 * burst is finished. That single property is what produces the convoy effect,
 * where a short job stuck behind a long one waits far longer than it needs to.
 *
 * The `__attribute__((unused))` below simply tells the compiler not to warn
 * when your own algorithm stops calling this function. You may delete the
 * whole function once you no longer need it as a reference.
 */
__attribute__((unused))                 /* harmless once you stop calling it */
static PCB *fcfs_choose(Sim *sim, PCB *running)
{
    if (running != NULL && running->remaining > 0)
        return running;                 /* non-preemptive: keep the CPU */

    PCB *best = NULL;
    for (int i = 0; i < sim->n; i++) {
        PCB *p = &sim->proc[i];
        if (p->arrival > sim->clock || p->remaining <= 0)
            continue;                   /* not arrived, or already finished */
        if (best == NULL ||
            p->arrival < best->arrival ||
            (p->arrival == best->arrival && p->pid < best->pid))
            best = p;                   /* earliest arrival, ties by lowest PID */
    }
    return best;
}

/* ===================================================================== */
/*  TODO 1: YOUR scheduling decision                                     */
/* ===================================================================== */
/*
 * Return the process that should run at time sim->clock, or NULL if nothing
 * is ready (the CPU idles this tick).
 *
 * A process is a candidate when:
 *     p->arrival <= sim->clock      it has arrived, and
 *     p->remaining > 0              it still has work left
 *
 * `running` is whatever you returned last tick, or NULL.
 *
 * Compared with the FCFS example above, only three things ever change:
 *
 *   1. the comparison that decides which candidate is "best"
 *        SJF / SRTF   smaller burst / smaller remaining wins
 *        Priority     smaller priority number wins
 *
 *   2. whether the choice can change while a process is still running
 *        non-preemptive   keep the early return, as FCFS does
 *        preemptive       delete it, and re-decide every tick
 *
 *   3. Round Robin and the multilevel algorithms also need to remember how
 *      long the current process has held the CPU. A "static int" local is a
 *      reasonable way to keep that count between calls.
 *
 * ALWAYS break ties by the lowest PID, or your Gantt chart will not be
 * reproducible.
 */
static PCB *choose_next(Sim *sim, PCB *running)
{
    /* TODO: replace this with your own algorithm.
     *
     * While you are getting the build working you can leave the line below,
     * which simply runs the baseline. Your submission must NOT still call
     * fcfs_choose(): implementing your own algorithm is the assignment. */
    return fcfs_choose(sim, running);
}

/* ===================================================================== */
/*  TODO 2: the extra output your topic requires                         */
/* ===================================================================== */
/*
 * Each topic asks for something beyond the standard tables: a comparison
 * against another algorithm, a list of preemption events, the queue a process
 * sat in, and so on. Print it here. See your ASSIGNMENT.md for what is needed.
 */
static void print_topic_extra(const Sim *sim)
{
    /* REQUIRED for every topic: the comparison against the FCFS baseline.
     * This one line gives you the whole table; see simlib.c if you want to
     * know how the baseline is computed. */
    print_vs_fcfs(sim, ALGORITHM_NAME);

    /* TODO: your topic's own extra output, on top of the comparison above.
     *       See your ASSIGNMENT.md for what is required. */
}

/* ===================================================================== */
/*  The main loop. You should not need to change anything below here,    */
/*  although you are welcome to.                                         */
/* ===================================================================== */

static void admit_arrivals(Sim *sim)
{
    for (int i = 0; i < sim->n; i++) {
        PCB *p = &sim->proc[i];
        if (p->state == P_NEW && p->arrival <= sim->clock)
            set_state(sim, p, P_READY);
    }
}

static void simulate(Sim *sim)
{
    PCB *running  = NULL;   /* process still holding the CPU, NULL if it ended */
    int  last_pid = -1;     /* pid dispatched on the previous busy tick        */

    while (!all_done(sim) && sim->clock < MAX_TIME) {
        admit_arrivals(sim);

        PCB *next = choose_next(sim, running);

        if (next == NULL) {
            run_idle_tick(sim);
            running = NULL;
            continue;
        }

        /* A context switch is the CPU being handed to a DIFFERENT process.
         * Note this is compared against last_pid, not against `running`:
         * when a process terminates, `running` becomes NULL, and dispatching
         * the next process is still a context switch. */
        if (last_pid != -1 && next->pid != last_pid)
            sim->context_switches++;

        /* The process leaving the CPU goes back to READY unless it finished. */
        if (running != NULL && running != next && running->remaining > 0)
            set_state(sim, running, P_READY);

        run_one_tick(sim, next);

        last_pid = next->pid;
        running  = (next->remaining > 0) ? next : NULL;
    }

    if (sim->clock >= MAX_TIME)
        fprintf(stderr, "warning: simulation hit the %d time-unit limit\n", MAX_TIME);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <workload-file> [-v] [-q QUANTUM]\n"
                "  -v          print every process state transition\n"
                "  -q QUANTUM  time quantum, for Round Robin and MLFQ\n",
                argv[0]);
        return 2;
    }

    static Sim sim;             /* static: it is large, and zero-initialised */
    sim.verbose = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            sim.verbose = 1;
        } else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            TIME_QUANTUM = atoi(argv[++i]);
            if (TIME_QUANTUM < 1) {
                fprintf(stderr, "quantum must be at least 1\n");
                return 2;
            }
        } else {
            fprintf(stderr, "unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    if (load_workload(&sim, argv[1]) != 0)
        return 1;
    if (start_processes(&sim) != 0)
        return 1;

    printf("ALGORITHM: %s\n", ALGORITHM_NAME);
    print_input_table(&sim);

    if (sim.verbose)
        printf("\nSTATE TRANSITIONS\n");

    simulate(&sim);

    print_gantt(&sim);
    print_metrics_table(&sim);
    print_summary(&sim);
    print_state_history(&sim);
    print_topic_extra(&sim);

    write_csv(&sim, "scheduling_report.csv");
    printf("\nCSV written to scheduling_report.csv\n");

    stop_processes(&sim);
    return 0;
}
