/*
 * pcb.h -- Process Control Block and shared declarations.
 *
 * Programming Assignment 2, CSCM602055 Operating Systems.
 * GIVEN FILE: you do not normally need to change this.
 *
 * The PCB is the structure the lecture (W03, Ch 3.5) describes: the record the
 * operating system keeps for every process. Ours is a simplified version with
 * the fields a scheduler actually needs.
 */

#ifndef PCB_H
#define PCB_H

#include <pthread.h>
#include <semaphore.h>

#define MAX_PROCESSES   64      /* plenty for the test cases */
#define MAX_TIME       512      /* simulation clock limit, a safety net */
#define IDLE            -1      /* Gantt entry when the CPU ran nothing */

/*
 * Process states, exactly as in the lecture's process state diagram.
 * A process is NEW before its arrival time, READY once it has arrived and is
 * waiting in the ready queue, RUNNING while it holds the CPU, and TERMINATED
 * once its burst is finished.
 */
/* Forward declaration: a PCB needs a way back to the shared state so that a
 * process thread can signal the scheduler when its time unit is over. */
struct Sim;

typedef enum {
    P_NEW = 0,
    P_READY,
    P_RUNNING,
    P_TERMINATED
} ProcState;

/*
 * One process. Fields marked "given" are filled in by the workload loader;
 * fields marked "you" are the ones your scheduler must maintain.
 */
typedef struct {
    /* --- given: read from the workload file --- */
    int pid;                /* process id, 1..n                              */
    int arrival;            /* arrival time                                  */
    int burst;              /* total CPU time required                       */
    int priority;           /* smaller number = higher priority              */

    /* --- you: maintained while the simulation runs --- */
    int remaining;          /* CPU time still needed; starts equal to burst   */
    int first_run;          /* time of first dispatch, -1 until it runs       */
    int completion;         /* time the process finished, -1 until it does    */
    ProcState state;        /* current state                                  */
    int queue_level;        /* only used by MLQ / MLFQ; 0 is the top queue    */

    /* --- given: the concurrency machinery, already wired up for you --- */
    pthread_t tid;          /* the thread representing this process           */
    sem_t     run_sem;      /* scheduler posts this to grant one time unit    */
    struct Sim *owner;      /* back-pointer to the shared simulation state    */
} PCB;

/*
 * Everything the simulation shares. In a real kernel this would be global
 * kernel state; here it is one struct so that it is obvious what is shared
 * between threads, and therefore what needs protecting.
 */
typedef struct Sim {
    PCB   proc[MAX_PROCESSES];
    int   n;                      /* number of processes                     */

    int   gantt[MAX_TIME];        /* gantt[t] = pid that ran at time t, or IDLE */
    int   clock;                  /* current simulation time                 */
    int   context_switches;       /* counted by the scheduler                */
    int   busy_ticks;             /* time units where the CPU was not idle   */

    sem_t tick_done;              /* a worker posts this when its tick is over */
    pthread_mutex_t log_mutex;    /* protects the shared log/counters below  */

    int   verbose;                /* 1 = print every state transition        */
} Sim;

#endif /* PCB_H */
