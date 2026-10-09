/*
 * fork_vs_thread.c -- a short demonstration for Programming Assignment 2.
 *
 * Part of the assignment, but deliberately small. It exists to make one point
 * from the W03 lecture concrete:
 *
 *     threads of one process SHARE memory;
 *     a process created by fork() gets its own COPY.
 *
 * That difference is the whole reason your scheduler needs semaphores. The
 * process threads in scheduler.c all see the same Sim struct, so without the
 * semaphore handshake they would race each other.
 *
 * Build:  make fork_vs_thread
 * Run:    ./fork_vs_thread
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>

/* One shared counter, used by both halves of the demonstration. */
static int counter = 0;

#define BUMPS 100000

static void *thread_bump(void *arg)
{
    (void)arg;
    for (int i = 0; i < BUMPS; i++)
        counter++;                  /* deliberately unprotected */
    return NULL;
}

int main(void)
{
    printf("=== Part 1: two THREADS sharing one counter ===\n");
    counter = 0;

    pthread_t a, b;
    pthread_create(&a, NULL, thread_bump, NULL);
    pthread_create(&b, NULL, thread_bump, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);

    printf("  expected : %d\n", 2 * BUMPS);
    printf("  actual   : %d\n", counter);
    if (counter != 2 * BUMPS)
        printf("  -> the counter is WRONG: the two threads raced on counter++\n");
    else
        printf("  -> no race observed this run (try again; a race is not\n"
               "     guaranteed to show itself every time, which is exactly\n"
               "     what makes race conditions hard to find)\n");

    /* ================================================================
     * TODO: complete Part 2.
     *
     * Use fork() to create a child process. Have the CHILD set
     *     counter = 999;
     * and print it, then _exit(0). Have the PARENT wait() for the child
     * and then print counter again.
     *
     * Before you run it, write down what you expect the parent to print.
     * Then run it and explain the result in your report.
     * ================================================================ */

    printf("\n=== Part 2: a forked PROCESS with its own copy ===\n");
    printf("  TODO: implement this part (see the comment above)\n");

    return 0;
}
