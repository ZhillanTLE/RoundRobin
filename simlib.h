/*
 * simlib.h -- helpers provided for Programming Assignment 2.
 * GIVEN FILE: you do not need to change this.
 */

#ifndef SIMLIB_H
#define SIMLIB_H

#include "pcb.h"

/* --- setting up ------------------------------------------------------- */

/* Read a workload file into sim. Returns 0 on success, -1 on failure.
 * File format: one process per line, "pid arrival burst priority".
 * Lines starting with '#' and blank lines are ignored. */
int  load_workload(Sim *sim, const char *path);

/* Create one thread per process and initialise every semaphore. */
int  start_processes(Sim *sim);

/* Let every thread finish and clean up. Call this when the run is over. */
void stop_processes(Sim *sim);

/* --- running one time unit -------------------------------------------- */

/* Give process `p` the CPU for exactly one time unit and wait for it.
 * This is the dispatcher: it posts p->run_sem and blocks on sim->tick_done.
 * Returns when the tick is complete. */
void run_one_tick(Sim *sim, PCB *p);

/* Record that the CPU was idle for one time unit. */
void run_idle_tick(Sim *sim);

/* Change a process's state, printing the transition when verbose is on. */
void set_state(Sim *sim, PCB *p, ProcState s);

const char *state_name(ProcState s);

/* --- output ------------------------------------------------------------ */

void print_input_table(const Sim *sim);
void print_gantt(const Sim *sim);
void print_metrics_table(const Sim *sim);
void print_summary(const Sim *sim);
void print_state_history(const Sim *sim);

/* Write the per-process metrics to a CSV file. Returns 0 on success. */
int  write_csv(const Sim *sim, const char *path);

/* --- the FCFS baseline ------------------------------------------------- */

/* Average metrics for one run. */
typedef struct {
    double wt, tat, rt;
    int    switches;
    int    total_time;
} Averages;

/* Compute the averages for the run that has just finished. */
Averages measure(const Sim *sim);

/* Compute what FCFS WOULD have done on the same workload, without threads and
 * without disturbing the current simulation. This is the baseline every topic
 * compares against, and it is given to you so that you only have to implement
 * your own algorithm. */
Averages fcfs_baseline(const Sim *sim);

/* Print the standard "your algorithm vs FCFS" table, plus the percentage
 * change on each metric. */
void print_vs_fcfs(const Sim *sim, const char *algorithm_name);

/* --- small conveniences ------------------------------------------------ */

/* Has every process finished? */
int  all_done(const Sim *sim);

/* Number of processes that have arrived by time t and are not finished. */
int  count_ready(const Sim *sim, int t);

#endif /* SIMLIB_H */
