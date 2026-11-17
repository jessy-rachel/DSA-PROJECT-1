# DSA Project 1 — Week 3 experiments

These two add-on source files are a **separate experiment simulator**, inspired by the group's `stack.c` and `student_b_queue.c`. The existing Week 1/2 programs stay unchanged.

## Files to add to your existing GitHub repository
- `week3_experiment.c` — implements Student A undo stack and Student B redo queue; one common measurement model. Reads the already-present `samples.h`.
- `run_week3.py` — compiles C, runs all combinations, creates CSV tables and four charts.

## Run in Codespaces (repo root)
```bash
python3 -m pip install matplotlib  # only if matplotlib is unavailable
python3 run_week3.py
```
You can also compile and run a single case manually:
```bash
gcc -std=c11 -O2 -Wall -Wextra week3_experiment.c -o week3_experiment
./week3_experiment stack 50 20000 42
./week3_experiment queue 50 20000 42
```
The console row is CSV: `method,K,failure_denominator,seed,pass,fram_writes,fram_reads,recovery_steps,max_log_size,extra_steps,power_failures,checkpoint_failures,full_log_events,checkpoints`.

## Experimental design
- Methods: Student A stack/undo and Student B queue/redo.
- K: 10, 50, 100, 500 samples.
- Failure rates per atomic FRAM write: none, rare (1/100000), medium (1/20000), frequent (1/5000).
- 30 seeds per condition. 2 methods × 4 intervals × 4 rates × 30 seeds = 960 runs.
- Every run processes 2,000 input samples and checks `count=2000, sum=1026639, min=7, max=1341, average=513`.
- Full-log tests: force capacity to 5 entries and checkpoint when fewer than 5 entries are free. 10 seeds/method.
- Crash-in-checkpoint tests: scan seeds at 1/1000 failures per write and save the first run of each method where a failure occurs during checkpoint processing.

## Result files
- `week3_results.csv` — one row per normal experiment (960 rows).
- `week3_summary.csv` — aggregates by method/K/failure rate.
- `week3_full_log_test.csv` — 20 explicit small-capacity experiments.
- `week3_checkpoint_crashes.csv` — one forced checkpoint-interruption example for each method.
- `week3_writes.png`, `week3_extra_steps.png`, `week3_recovery.png`, `week3_max_log.png` — figures.

## Metric definitions
- FRAM writes: total atomic FRAM writes performed, including writes immediately followed by a power failure.
- FRAM reads: explicit reads through read_fram(), including measurement of state/metadata.
- Recovery steps: undo entries popped or redo entries reapplied **during recovery**, not ordinary checkpoint replay.
- Largest log: maximum published number of stack/queue entries.
- Extra steps: sample-processing attempts beyond the 2,000 required steps (repeated after failed intervals).
- Full-log events: count of early checkpoints used to prevent a fixed-size log overflow.
- Checkpoint failures: number of failures occurring during the checkpoint routine.

## Comparability / assumptions
- Both methods use the same uniform failure probability **per FRAM write**. Since methods write different numbers of times, their total numbers of failures may differ; this is an important limitation when interpreting averages.
- Queue method retains Student B's five-value batch per sample. Unlike their original `B_FRAM_SIZE 512`, the experiment allocates enough entries to support K=500 (2,500 queue entries).
- Stack method follows Student A's write-ahead undo semantics: `(variable index, old value)` is logged before changing live state.
- Each FRAM write is assumed atomic, with power failure possible immediately after it. The initial memory formatting is not included in write cost.
- Results represent the shared C simulator model, **not physical FRAM energy or timing measurements**. Recovery steps are an operation count, not seconds.
- Normal runs exercise a log capacity of 8,188 entries. The separate full-log test uses five entries and makes an early safe checkpoint; this is an added overflow-handling policy for the experiment.
- Student C's dirty-flag bonus is available in the repository as `platform_dirty.c`, but is **not included** in these stack/queue CSV comparisons. If all three methods must be compared, test the dirty-flag method separately under the same experimental conditions.
