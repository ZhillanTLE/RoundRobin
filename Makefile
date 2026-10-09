CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -pthread
LDLIBS  = -pthread

all: scheduler fork_vs_thread

scheduler: scheduler.c simlib.c simlib.h pcb.h
	$(CC) $(CFLAGS) -o $@ scheduler.c simlib.c $(LDLIBS)

fork_vs_thread: fork_vs_thread.c
	$(CC) $(CFLAGS) -o $@ fork_vs_thread.c $(LDLIBS)

run: scheduler
	./scheduler tests/tc1.txt

test: scheduler
	@for f in tests/tc*.txt; do \
		echo "=== $$f ==="; ./scheduler $$f | tail -12; \
	done

clean:
	rm -f scheduler fork_vs_thread scheduling_report.csv

.PHONY: all run test clean
