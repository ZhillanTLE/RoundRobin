/*
 * simlib.c -- helpers provided for Programming Assignment 2.
 * GIVEN FILE: you do not need to change this, but you should read it.
 *
 * This file contains the parts of the simulation that are the same whichever
 * scheduling algorithm you implement: loading the workload, creating one
 * thread per process, handing the CPU to a process for one time unit, and
 * printing the results.
 *
 * The interesting part for the assignment is in scheduler.c.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "simlib.h"

/* ------------------------------------------------------------ workload */

int load_workload(Sim *sim, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "cannot open workload '%s'\n", path);
        return -1;
    }

    sim->n = 0;
    char line[256];

    while (fgets(line, sizeof line, fp) != NULL) {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        if (sim->n >= MAX_PROCESSES) {
            fprintf(stderr, "too many processes (max %d)\n", MAX_PROCESSES);
            fclose(fp);
            return -1;
        }

        PCB *p = &sim->proc[sim->n];
        int got = sscanf(line, "%d %d %d %d",
                         &p->pid, &p->arrival, &p->burst, &p->priority);
        if (got < 3) {
            fprintf(stderr, "bad line in workload: %s", line);
            fclose(fp);
            return -1;
        }
        if (got == 3)
            p->priority = 0;           /* priority is optional */

        if (p->burst <= 0) {
            fprintf(stderr, "process %d has a burst of %d; must be > 0\n",
                    p->pid, p->burst);
            fclose(fp);
            return -1;
        }

        p->remaining   = p->burst;
        p->first_run   = -1;
        p->completion  = -1;
        p->state       = P_NEW;
        p->queue_level = 0;
        sim->n++;
    }
    fclose(fp);

    if (sim->n == 0) {
        fprintf(stderr, "workload '%s' contains no processes\n", path);
        return -1;
    }

    sim->clock = 0;
    sim->context_switches = 0;
    sim->busy_ticks = 0;
    for (int t = 0; t < MAX_TIME; t++)
        sim->gantt[t] = IDLE;

    return 0;
}

/* ------------------------------------------------------------- threads */

/*
 * The body of every process thread.
 *
 * A process thread does nothing at all until the scheduler grants it a time
 * unit by posting its run_sem. It then "uses the CPU" for one unit, decrements
 * the work it has left, and posts tick_done to tell the scheduler the unit is
 * over. Then it waits again.
 *
 * This is why the simulation is deterministic even though it uses real
 * threads: at most one thread is ever runnable at a time, because the
 * scheduler releases exactly one semaphore and then waits.
 */
static void *process_thread(void *arg)
{
    PCB *p = (PCB *)arg;

    for (;;) {
        sem_wait(&p->run_sem);

        if (p->remaining <= 0)      /* the scheduler is telling us to exit */
            break;

        p->remaining--;

        /* A real process would do work here. We only need the bookkeeping,
         * so the thread immediately hands control back to the scheduler. */
        sem_post(&p->owner->tick_done);
    }
    return NULL;
}

int start_processes(Sim *sim)
{
    if (sem_init(&sim->tick_done, 0, 0) != 0) {
        perror("sem_init");
        return -1;
    }
    if (pthread_mutex_init(&sim->log_mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        return -1;
    }

    for (int i = 0; i < sim->n; i++) {
        PCB *p = &sim->proc[i];
        p->owner = sim;
        if (sem_init(&p->run_sem, 0, 0) != 0) {
            perror("sem_init");
            return -1;
        }
        if (pthread_create(&p->tid, NULL, process_thread, p) != 0) {
            perror("pthread_create");
            return -1;
        }
    }
    return 0;
}

void stop_processes(Sim *sim)
{
    /* Wake every thread one last time so it can see remaining <= 0 and exit.
     * Without this the threads would block on their semaphore forever and
     * pthread_join would never return. */
    for (int i = 0; i < sim->n; i++) {
        sim->proc[i].remaining = 0;
        sem_post(&sim->proc[i].run_sem);
    }
    for (int i = 0; i < sim->n; i++) {
        pthread_join(sim->proc[i].tid, NULL);
        sem_destroy(&sim->proc[i].run_sem);
    }
    sem_destroy(&sim->tick_done);
    pthread_mutex_destroy(&sim->log_mutex);
}

/* ---------------------------------------------------------- dispatching */

const char *state_name(ProcState s)
{
    switch (s) {
        case P_NEW:        return "NEW";
        case P_READY:      return "READY";
        case P_RUNNING:    return "RUNNING";
        case P_TERMINATED: return "TERMINATED";
        default:           return "?";
    }
}

void set_state(Sim *sim, PCB *p, ProcState s)
{
    if (p->state == s)
        return;

    /* The state log and the counters are touched by the scheduler while the
     * process threads are alive, so the update is done under a mutex. On this
     * simulation only one thread runs at a time, but the lock documents the
     * shared data and keeps the program correct if that ever changes. */
    pthread_mutex_lock(&sim->log_mutex);
    if (sim->verbose)
        printf("  t=%-3d P%-2d %-10s -> %s\n",
               sim->clock, p->pid, state_name(p->state), state_name(s));
    p->state = s;
    pthread_mutex_unlock(&sim->log_mutex);
}

void run_one_tick(Sim *sim, PCB *p)
{
    if (p->first_run < 0)
        p->first_run = sim->clock;

    set_state(sim, p, P_RUNNING);

    pthread_mutex_lock(&sim->log_mutex);
    sim->gantt[sim->clock] = p->pid;
    sim->busy_ticks++;
    pthread_mutex_unlock(&sim->log_mutex);

    /* Hand the CPU over and wait for the time unit to finish. */
    sem_post(&p->run_sem);
    sem_wait(&sim->tick_done);

    sim->clock++;

    if (p->remaining == 0) {
        p->completion = sim->clock;
        set_state(sim, p, P_TERMINATED);
    }
}

void run_idle_tick(Sim *sim)
{
    pthread_mutex_lock(&sim->log_mutex);
    sim->gantt[sim->clock] = IDLE;
    pthread_mutex_unlock(&sim->log_mutex);
    sim->clock++;
}

/* -------------------------------------------------------------- queries */

int all_done(const Sim *sim)
{
    for (int i = 0; i < sim->n; i++)
        if (sim->proc[i].remaining > 0)
            return 0;
    return 1;
}

int count_ready(const Sim *sim, int t)
{
    int c = 0;
    for (int i = 0; i < sim->n; i++)
        if (sim->proc[i].arrival <= t && sim->proc[i].remaining > 0)
            c++;
    return c;
}

/* -------------------------------------------------------- FCFS baseline */

Averages measure(const Sim *sim)
{
    Averages a = { 0, 0, 0, sim->context_switches, sim->clock };
    for (int i = 0; i < sim->n; i++) {
        const PCB *p = &sim->proc[i];
        int tat = p->completion - p->arrival;
        a.tat += tat;
        a.wt  += tat - p->burst;
        a.rt  += p->first_run - p->arrival;
    }
    a.wt /= sim->n; a.tat /= sim->n; a.rt /= sim->n;
    return a;
}

/*
 * What FCFS would have done on this workload.
 *
 * This is a plain arithmetic replay of the same tick loop, with no threads and
 * no semaphores: it only needs to work out the schedule, not simulate it. It
 * does not touch the live Sim, so it is safe to call after your own run has
 * finished.
 *
 * FCFS: non-preemptive, earliest arrival first, ties by lowest PID.
 */
Averages fcfs_baseline(const Sim *sim)
{
    int n = sim->n;
    int remaining[MAX_PROCESSES], first[MAX_PROCESSES], done[MAX_PROCESSES];

    for (int i = 0; i < n; i++) {
        remaining[i] = sim->proc[i].burst;
        first[i] = -1;
        done[i]  = -1;
    }

    int t = 0, finished = 0, cur = -1, last = -1, switches = 0, busy = 0;

    while (finished < n && t < MAX_TIME) {
        int pick = -1;

        if (cur >= 0 && remaining[cur] > 0) {
            pick = cur;                     /* non-preemptive: keep the CPU */
        } else {
            for (int i = 0; i < n; i++) {
                if (sim->proc[i].arrival > t || remaining[i] <= 0)
                    continue;
                if (pick < 0 ||
                    sim->proc[i].arrival < sim->proc[pick].arrival ||
                    (sim->proc[i].arrival == sim->proc[pick].arrival &&
                     sim->proc[i].pid < sim->proc[pick].pid))
                    pick = i;
            }
        }

        if (pick < 0) { t++; continue; }    /* nothing ready: CPU idles */

        if (last != -1 && pick != last) switches++;
        if (first[pick] < 0) first[pick] = t;

        remaining[pick]--;
        t++;
        busy++;
        last = pick;
        cur  = pick;
        if (remaining[pick] == 0) { done[pick] = t; finished++; cur = -1; }
    }

    Averages a = { 0, 0, 0, switches, t };
    for (int i = 0; i < n; i++) {
        int tat = done[i] - sim->proc[i].arrival;
        a.tat += tat;
        a.wt  += tat - sim->proc[i].burst;
        a.rt  += first[i] - sim->proc[i].arrival;
    }
    a.wt /= n; a.tat /= n; a.rt /= n;
    return a;
}

/* Percentage change from the baseline. Negative means an improvement for a
 * metric where lower is better, which is true of all three here. */
static double pct(double mine, double base)
{
    if (base == 0.0)
        return 0.0;
    return (mine - base) * 100.0 / base;
}

void print_vs_fcfs(const Sim *sim, const char *algorithm_name)
{
    Averages me   = measure(sim);
    Averages base = fcfs_baseline(sim);

    printf("\n===============================================================\n");
    printf("            %s  vs  FCFS (baseline)\n", algorithm_name);
    printf("===============================================================\n");
    printf("Metric                    FCFS   %-12.12s     Change\n", algorithm_name);
    printf("---------------------------------------------------------------\n");
    printf("Average waiting time    %7.2f   %11.2f   %+8.1f%%\n",
           base.wt,  me.wt,  pct(me.wt,  base.wt));
    printf("Average turnaround time %7.2f   %11.2f   %+8.1f%%\n",
           base.tat, me.tat, pct(me.tat, base.tat));
    printf("Average response time   %7.2f   %11.2f   %+8.1f%%\n",
           base.rt,  me.rt,  pct(me.rt,  base.rt));
    printf("Context switches        %7d   %11d   %+8.1f%%\n",
           base.switches, me.switches,
           pct((double)me.switches, (double)base.switches));
    printf("Total simulation time   %7d   %11d\n",
           base.total_time, me.total_time);
    printf("===============================================================\n");
    printf("A negative change is an improvement for the first three rows,\n");
    printf("because lower waiting, turnaround and response times are better.\n");
    printf("A positive change in context switches is a cost, not a benefit.\n");
}

/* --------------------------------------------------------------- output */

void print_input_table(const Sim *sim)
{
    printf("=================================================\n");
    printf("                 PROCESS INPUT\n");
    printf("=================================================\n");
    printf("PID       Arrival Time     Burst Time    Priority\n");
    printf("-------------------------------------------------\n");
    for (int i = 0; i < sim->n; i++)
        printf("P%-8d %8d %14d %11d\n",
               sim->proc[i].pid, sim->proc[i].arrival,
               sim->proc[i].burst, sim->proc[i].priority);
    printf("=================================================\n");
}

/*
 * Print the Gantt chart by collapsing runs of equal pids into one block, so
 * "P1 P1 P1 P2" becomes one P1 block of width 3 and one P2 block.
 *
 * Block width is capped: an unimplemented scheduler idles for the whole
 * simulation, and a single 512-unit block would print as one unreadable line
 * that wraps across the terminal. The cap is wider than any block a real
 * workload here produces, so normal output is unaffected.
 */
#define MAX_BLOCK_WIDTH 60
void print_gantt(const Sim *sim)
{
    printf("\nCPU EXECUTION TIMELINE\n");

    int t = 0;
    /* top row: the blocks */
    printf("|");
    while (t < sim->clock) {
        int end = t;
        while (end < sim->clock && sim->gantt[end] == sim->gantt[t])
            end++;
        int width = (end - t) * 3;
        if (width < 5) width = 5;
        if (width > MAX_BLOCK_WIDTH) width = MAX_BLOCK_WIDTH;

        char label[16];
        if (sim->gantt[t] == IDLE)
            snprintf(label, sizeof label, "idle");
        else
            snprintf(label, sizeof label, "P%d", sim->gantt[t]);

        int pad = width - (int)strlen(label);
        int left = pad / 2, right = pad - left;
        for (int i = 0; i < left; i++) putchar(' ');
        fputs(label, stdout);
        for (int i = 0; i < right; i++) putchar(' ');
        printf("|");
        t = end;
    }

    /* bottom row: the times where each block starts, plus the final time */
    printf("\n0");
    t = 0;
    while (t < sim->clock) {
        int end = t;
        while (end < sim->clock && sim->gantt[end] == sim->gantt[t])
            end++;
        int width = (end - t) * 3;
        if (width < 5) width = 5;
        if (width > MAX_BLOCK_WIDTH) width = MAX_BLOCK_WIDTH;

        char stamp[16];
        snprintf(stamp, sizeof stamp, "%d", end);
        int pad = width + 1 - (int)strlen(stamp);
        for (int i = 0; i < pad; i++) putchar(' ');
        fputs(stamp, stdout);
        t = end;
    }
    printf("\n");
}

void print_metrics_table(const Sim *sim)
{
    printf("\n=======================================================\n");
    printf("                  SCHEDULING TABLE\n");
    printf("=======================================================\n");
    printf("PID    AT    BT    CT   TAT    WT    RT\n");
    printf("-------------------------------------------------------\n");
    for (int i = 0; i < sim->n; i++) {
        const PCB *p = &sim->proc[i];
        int tat = p->completion - p->arrival;
        int wt  = tat - p->burst;
        int rt  = p->first_run - p->arrival;
        printf("P%-4d %4d %5d %5d %5d %5d %5d\n",
               p->pid, p->arrival, p->burst, p->completion, tat, wt, rt);
    }
    printf("=======================================================\n");
}

void print_summary(const Sim *sim)
{
    double tat = 0, wt = 0, rt = 0;
    for (int i = 0; i < sim->n; i++) {
        const PCB *p = &sim->proc[i];
        int t = p->completion - p->arrival;
        tat += t;
        wt  += t - p->burst;
        rt  += p->first_run - p->arrival;
    }

    printf("\n=================================================\n");
    printf("            SCHEDULING PERFORMANCE\n");
    printf("=================================================\n");
    printf("Average Waiting Time     : %.2f\n", wt  / sim->n);
    printf("Average Turnaround Time  : %.2f\n", tat / sim->n);
    printf("Average Response Time    : %.2f\n", rt  / sim->n);
    printf("CPU Utilization          : %.2f%%\n",
           sim->clock > 0 ? 100.0 * sim->busy_ticks / sim->clock : 0.0);
    printf("Throughput               : %.2f process/time unit\n",
           sim->clock > 0 ? (double)sim->n / sim->clock : 0.0);
    printf("Total Context Switch     : %d\n", sim->context_switches);
    printf("Total Simulation Time    : %d\n", sim->clock);
    printf("=================================================\n");
}

void print_state_history(const Sim *sim)
{
    printf("\nFINAL PROCESS STATES\n");
    for (int i = 0; i < sim->n; i++)
        printf("  P%-3d : %s\n", sim->proc[i].pid, state_name(sim->proc[i].state));
}

int write_csv(const Sim *sim, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "cannot write '%s'\n", path);
        return -1;
    }
    fprintf(fp, "pid,arrival,burst,priority,completion,turnaround,waiting,response\n");
    for (int i = 0; i < sim->n; i++) {
        const PCB *p = &sim->proc[i];
        int tat = p->completion - p->arrival;
        fprintf(fp, "%d,%d,%d,%d,%d,%d,%d,%d\n",
                p->pid, p->arrival, p->burst, p->priority,
                p->completion, tat, tat - p->burst, p->first_run - p->arrival);
    }
    fclose(fp);
    return 0;
}
