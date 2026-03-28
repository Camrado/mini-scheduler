# -*- Makefile -*-
CC = gcc
CFLAGS = -w -g
LIBS =

TARGET = sched

HELPERS = heap.o

SCHED_OBJ = sched.o
TRACE_OBJ = trace.o

all: $(TARGET)
	@echo "Compiler version:"
	@$(CC) --version

$(TARGET): $(HELPERS) $(SCHED_OBJ) $(TRACE_OBJ)
	@echo "Building $@"
	@$(CC) $(SCHED_OBJ) $(TRACE_OBJ) $(HELPERS) $(LDFLAGS) $(CFLAGS) -o $@

sched.o: sched.c sched.h trace.h process.h heap.h
	@echo "Building $@"
	@$(CC) $(CFLAGS) -c sched.c

heap.o: heap.c heap.h process.h
	@echo "Building $@"
	@$(CC) $(CFLAGS) -c heap.c

.c.o:
	@echo "Building $@"
	@$(CC) $(CFLAGS) -c $<

clean:
	rm -rf $(TARGET) *.o *.dSYM *~