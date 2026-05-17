
# DSA2 Project 2526

The following README is to be completed by you.
You MUST keep the following statement in the document and sign it by writting your full name.

##  Declaration of Authorship
> I hereby certify that the work submitted is my own. While I may have occasionally used artificial intelligence tools for assistance, I have carefully reviewed and verified the submitted source code and written report. I fully understand their content and take complete responsibility for the work presented.

(Add all logins used for committing)
* Name1: Dmitriy Kuramshin (DmitriyKuramshin)
* Name2: Kamal Yalchin (Camrado, Kamal Y)
* Name3: Luis Markus Torres (LuisMarkusTorres, Luis Markus Torres)


## Subject

The subject is described on the Moodle page of the course.

## Tests

### Test Files Description

Let us assume your project's structure is as follows:

```
dsa2-project
├── src
│   ├── Makefile
│   ├── sched.c
│   ├── ...
│   └── sched
└── tests
     ├── out
     │   ├── test-0.out
     │   ├── test-0.res
     │   ├── ...
     ├── in
     |   ├── test-0.in
     |   ├── ...
     ├── ref
     │   ├── test-0.ref
     │   ├── ...
     │   └── test-10.ref
     ├── src
     │   ├── Makefile
     │   ├── read_trace.c
     │   ├── read_trace.h
     │   ├── test_main.c
     │   ├── workload.c
     │   └── workload.h
     └── test-sched.sh
```

Your source files must be in the top `src/` directory.
Complete the existing `Makefile` do not write a new one from scratch.
You will find test a test program to build in `tests/src` with `make`.
Once build, you will be able to test with the `tests/test-sched.sh` script.


Description of the data directories:
- `in` : the input data files
- `ref` : the expected output.
- `out` :  initially contains  the full output  of my  scheduler (with
  explained  decisions). Once  you run  the  test script,  it will  be
  overwritten by your program outputs.


## Test Principle
The principle is to analyze an output such as:
```
* Parsed trace (from out/test-8.res):
 0: RRRRRR.......RRRRR___________
 1: ..RRRRRRRRRRR________________
 2: ....RR.R.R.R.RRRRRR__________
 3: ......RRRRRRRRRRR____________
```
and check that:
1. at any timestep, the cumulative load of scheduled processes does not exceed the CPU capacity. In the example above, at timestep=4 (processes 0,1 and 2 are running), we can get their priorities in the `in/test-8.in` data, and sum them.
2. the makespan of the execution (i.e the walltime) does not last more than a given limit, specified per test.
3. the number of execution quantums match the total sum of processes' durations. An execution quantum is an 'R' in the trace.
4. the property "At a given timestep, the running processes that can fit in the CPU capacity all have a greater of equal priority than the ones in pending queue" holds.

Note that `test-sched.sh` uses the auxiliary C program specified in the script as: `BIN_TEST=src/test_main`. This program is passed the maximum CPU capacity and walltime limit and make the above verifications.
Note that the expected maximum walltimes for the produced schedule are hard-coded in the script as:
```shell
MAX_DURATIONS=(15 20 24 16 29 19 21 25 19 24 29)
```
for respectively `in/test-[0,1,2,...,10]` and are passed as parameter to the `BIN_TEST` program.


## How to use `test-sched.sh`

From the top directory of your project:
1. `cd tests/src`
2. `make`   # This compiles the `test_main` C program
3. `cd ..`
4. Run `./test-sched.sh`



## Report

### Algorithm Description

We are simulating a CPU that is able to run several processes at once.

In order to manage the processes we use two lists:

- The **runningQueue**, which is a list of processes that are currently running. They are placed in the order of lowest to highest priority. This makes it easier to find the process with a certain priority. If two processes have the same priority, their IDs are used to decide which one runs first, and the process with the higher ID runs first.

- The **pendingQueue**, which is a list of processes that are waiting to run. They, on the other hand, are placed in the order of highest to lowest priority. The lower ID process runs first if two processes have the same priority.

---

### Scheduling Round Overview

1. **Remove Finished Processes** Loops through the `runningQueue` and removes any process where `tf < current_time`. Each removal calls `heap_remove_at`, which swaps the selected slot with the last element, decreases the heap size, and calls `heapify_down` to restore the heap properties. We also decrease `prioritySum` by each removed process's priority.

2. **Add Newly Arrived Processes** Checks the entire process array for any entry where `ts == current_time`, and adds it to `pendingQueue`.

3. **Hold Queue** At the start of each scheduling round, we create a temporary `holdQueue`. Any process popped from a heap, but not permanently placed, either because it was preempted from the running queue or could not fit into the CPU, is collected here. We insert all held processes back into `pendingQueue` after the scheduling loop finishes. This avoids reevaluating a process in the same round and prevents infinite loops.

4. **Schedule** While `pendingQueue` is not empty, we pop the highest-priority candidate and apply one of the three scenarios:

- **Fits directly**: if `prioritySum + candidate.priority <= CPU_CAPABILITY`, we add the candidate to `runningQueue` and update `prioritySum`.

- **Does not fit: preemption possible**: if `candidate.priority > lowest_running.priority`, we insert the candidate into `runningQueue` without waiting (temporarily exceeding capacity). We then repeatedly pop the current minimum from `runningQueue`, moving each evicted process to `holdQueue` and incrementing its `tf` by 1 until `prioritySum <= CPU_CAPABILITY`.

- **Does not fit: no preemption**: if the candidate's priority is not strictly greater than the lowest running process (or the running queue is empty and the candidate alone exceeds capacity), we remove the candidate from `pendingQueue`, increment its `tf` by 1, and add it to `holdQueue`. The scheduling loop then continues with the next candidate in `pendingQueue`, meaning a lower-priority process that fits may still be scheduled in this round.

5. **Reinsert from holdQueue** After the round, we insert all processes in `holdQueue` back into `pendingQueue`, making sure it is a valid max-heap for the next round.

6. **Increment Idle Counters** For each process still in `pendingQueue` at the end of the round, we increase `idle` by 1. It is important that we already extended `tf` when a process was displaced (step 4), so we don't do it here.

7. **Record Timeline** We write the current contents of `runningQueue` and `pendingQueue`, along with any process that has not started yet (`ts > t`), into the timeline matrix. Each process is marked as `running`, `pending`, or `inactive` for this time step.

---

### Pseudo-Code

```
for t = 0 to END_STEP - 1:

    // Step 1 remove finished processes for each proc in runningQueue:

    for each proc in runningQueue:
    	if proc.tf < t:
            prioritySum -= proc.priority
            heap_remove_at(runningQueue, proc)

	// Step 2 enqueue newly arrived processes for each proc in procs[]:

		if proc.ts == t:
    	    max_heap_insert(pendingQueue, proc)

	holdQueue = []

	// Step 3 scheduling loop

	while pendingQueue is not empty:
		candidate = max_heap_top(pendingQueue)
		if prioritySum + candidate.priority <= CPU_CAPABILITY:
    		max_heap_pop(pendingQueue)
        	min_heap_insert(runningQueue, candidate)
        	prioritySum += candidate.priority
		else:
    		lowest = min_heap_top(runningQueue)
			if lowest != NULL and candidate.priority > lowest.priority:
    			// preempt: schedule candidate, then evict lowest(s) until under capacity
            	max_heap_pop(pendingQueue)
            	min_heap_insert(runningQueue, candidate)
            	prioritySum += candidate.priority

				while prioritySum > CPU_CAPABILITY:
					evict = min_heap_pop(runningQueue)
                    prioritySum -= evict.priority
                    evict.tf++
                    holdQueue.append(evict)
			else:
                // candidate cannot fit and cannot preempt anyone
                max_heap_pop(pendingQueue)
                candidate.tf++
                holdQueue.append(candidate)
                // note: loop continues, remaining pending candidates are still tried

	// Step 4 insert held processes back into pending

	for each proc in holdQueue:
    	max_heap_insert(pendingQueue, proc)

	// Step 5 increment idle counters for all waiting processes

	for each proc in pendingQueue:
    	proc.idle++

	// Step 6 record timeline state

	record_state(t, runningQueue, pendingQueue, procs[])
```
---

### Complexity of a Scheduling Round

Let `n` be the total number of processes.

**Best Case - O(n)**

If no process arrives or finishes and the pending queue is empty the scheduler will only perform two scans. With no heap operations the round runs in O(n).

**Average Case — O(n log n)**

In a typical round, a small number of processes will arrive (each insertion into `pendingQueue` costs O(log n)), a small number will finish (each `heap_remove_at` costs O(log n)), and the scheduling loop pops and possibly reinserts some candidates (each pop or insert costs O(log n)). If `m` heap operations occur across the round, the total cost is O(n + m log n). But since `m ≤ n`, this will give **O(n log n)**.

**Worst Case — O(n log n)**

In the worst case, all n processes arrive at the same timestep and every scheduling decision will trigger a preemption. Even so, each process can participate in at most a constant number of heap operations each round: one insert into `pendingQueue`, one pop from `pendingQueue`, one insert into `runningQueue`, and one removal from `runningQueue` (if preempted). Each operation is O(log n), so in all n processes the total complexity is O(n log n). Adding the O(n) arrival scan gives a worst-case complexity of **O(n log n)**.

---

 ### Project Status

 All 11 tests pass, with test-5 matching the `test-5.2.ref`. The scheduler fulfills all the project requirements:
 
 - Robust input handling
 
 - Capacity-bounded priority scheduling
 
 - Full preemption support
 
 - Accurate idle and delay tracking
 
 - Deterministic PID-based tie-breaking
 
 - Detailed and transparent logging
 
 No missing features or correctness issues remain.