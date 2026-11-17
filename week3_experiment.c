/* Week 3: Stack (undo) vs Queue (redo) — standalone experiment simulator.
   Based on the algorithms in the group's stack.c and student_b_queue.c.
   Build: gcc -std=c11 -O2 -Wall -Wextra week3_experiment.c -o week3_experiment
   Run:   ./week3_experiment stack 50 20000 42
          ./week3_experiment queue 500 20000 42
   Optional final number specifies smaller log capacity for full-log test.
   Failure probability: 1/fail_denominator per FRAM WRITE; zero disables failures.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#define MEMORY_SIZE 16384
#define N_SLOTS 5
#define STEP 0
#define SUM 1
#define MIN 2
#define MAX 3
#define COUNT 4
#include "samples.h"

/* Layout for stack: commit flag, top, 5 live fields, then pairs.
   Layout for queue: 5 committed fields, flag, count, then pairs. */
#define STACK_COMMIT 0
#define STACK_TOP 1
#define STACK_STATE 2
#define STACK_LOG 7
#define QUEUE_COMMIT 5
#define QUEUE_COUNT 6
#define QUEUE_LOG 7
#define MAX_CAP ((MEMORY_SIZE - 7) / 2)

typedef struct {
    uint64_t writes, reads, recovery_steps, attempts, failures;
    uint64_t full_log_events, checkpoint_failures, checkpoints;
    int max_log;
} Metrics;

static int32_t fram[MEMORY_SIZE], sram[N_SLOTS];
static Metrics m;
static bool failed, in_checkpoint, in_recovery;
static int fail_denominator, log_capacity;
static uint32_t rng;
static bool use_stack;

static uint32_t random_u32(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static int32_t read_fram(int index) {
    if (index < 0 || index >= MEMORY_SIZE) { fprintf(stderr,"bad FRAM read\n"); exit(2); }
    m.reads++;
    return fram[index];
}
static bool write_fram(int index, int32_t value) {
    if (index < 0 || index >= MEMORY_SIZE) { fprintf(stderr,"bad FRAM write\n"); exit(2); }
    if (failed) return false;
    fram[index] = value;
    m.writes++;
    if (fail_denominator > 0 && random_u32() % (uint32_t)fail_denominator == 0) {
        failed = true;
        m.failures++;
        if (in_checkpoint) m.checkpoint_failures++;
        memset(sram, 0, sizeof(sram));
        return false; /* write already happened, but power failed afterward */
    }
    return true;
}
static void record_log_size(int count) {
    if (count > m.max_log) m.max_log = count;
}
static void init_simulation(uint32_t seed) {
    memset(fram, 0, sizeof(fram));
    memset(sram, 0, sizeof(sram));
    memset(&m, 0, sizeof(m));
    failed = false;
    in_checkpoint = false;
    in_recovery = false;
    rng = seed ? seed : 1U;
    sram[MIN] = INT32_MAX;
    sram[MAX] = INT32_MIN;
    /* Initial FRAM formatting is excluded from per-run measured writes. */
    int base = use_stack ? STACK_STATE : 0;
    fram[base + MIN] = INT32_MAX;
    fram[base + MAX] = INT32_MIN;
}

/* ------------------ Student A: STACK / UNDO ------------------ */
static int stack_size(void) { return read_fram(STACK_TOP); }
static bool stack_push(int index, int32_t old_value) {
    int top = stack_size();
    if (top >= log_capacity) return false;
    int position = STACK_LOG + 2 * top;
    if (!write_fram(position, index)) return false;
    if (!write_fram(position + 1, old_value)) return false;
    if (!write_fram(STACK_TOP, top + 1)) return false;
    record_log_size(top + 1);
    return true;
}
static bool stack_pop(void) {
    int top = stack_size();
    if (top == 0) return true;
    int position = STACK_LOG + 2 * (top - 1);
    int index = read_fram(position);
    int32_t old_value = read_fram(position + 1);
    if (index < 0 || index >= N_SLOTS) return false;
    if (!write_fram(STACK_STATE + index, old_value)) return false;
    if (in_recovery) m.recovery_steps++;  /* one undo entry recovered */
    return write_fram(STACK_TOP, top - 1);
}
static bool stack_update(int index, int32_t value) {
    int32_t old_value = read_fram(STACK_STATE + index);
    if (old_value == value) return true;
    if (!stack_push(index, old_value)) return false;
    if (!write_fram(STACK_STATE + index, value)) return false;
    sram[index] = value;
    return true;
}
static bool stack_process_one(void) {
    int step = sram[STEP];
    int32_t sample = samples[step];
    if (!stack_update(SUM, sram[SUM] + sample)) return false;
    if (!stack_update(COUNT, sram[COUNT] + 1)) return false;
    if (sample < sram[MIN] && !stack_update(MIN, sample)) return false;
    if (sample > sram[MAX] && !stack_update(MAX, sample)) return false;
    return stack_update(STEP, step + 1);
}
static bool stack_checkpoint(void) {
    in_checkpoint = true;
    m.checkpoints++;
    if (!write_fram(STACK_COMMIT, 1)) { in_checkpoint = false; return false; }
    if (!write_fram(STACK_TOP, 0)) { in_checkpoint = false; return false; }
    bool ok = write_fram(STACK_COMMIT, 0);
    in_checkpoint = false;
    return ok;
}
static bool stack_recover(void) {
    if (read_fram(STACK_COMMIT) == 1) {
        if (!write_fram(STACK_TOP, 0)) return false;
        if (!write_fram(STACK_COMMIT, 0)) return false;
    } else {
        while (stack_size() > 0) if (!stack_pop()) return false;
    }
    for (int i = 0; i < N_SLOTS; ++i) sram[i] = read_fram(STACK_STATE + i);
    return true;
}

/* ------------------ Student B: QUEUE / REDO ------------------- */
/* Each sample appends all 5 NEW state values. Each batch is published only
   after all 10 data writes are complete, just like Student B's code. */
static int queue_size(void) { return read_fram(QUEUE_COUNT); }
static bool queue_append_state(void) {
    int count = queue_size();
    if (count + N_SLOTS > log_capacity) return false;
    for (int i = 0; i < N_SLOTS; ++i) {
        int pos = QUEUE_LOG + 2 * ((count + i) % log_capacity);
        if (!write_fram(pos, i)) return false;
        if (!write_fram(pos + 1, sram[i])) return false;
    }
    if (!write_fram(QUEUE_COUNT, count + N_SLOTS)) return false;
    record_log_size(count + N_SLOTS);
    return true;
}
static bool queue_process_one(void) {
    int32_t sample = samples[sram[STEP]];
    sram[SUM] += sample;
    sram[COUNT]++;
    if (sample < sram[MIN]) sram[MIN] = sample;
    if (sample > sram[MAX]) sram[MAX] = sample;
    sram[STEP]++;
    return queue_append_state();
}
static bool queue_apply(void) {
    int count = queue_size();
    for (int i = 0; i < count; ++i) {
        int pos = QUEUE_LOG + 2 * (i % log_capacity);
        int idx = read_fram(pos);
        if (idx < 0 || idx >= N_SLOTS) return false;
        int32_t value = read_fram(pos + 1);
        if (!write_fram(idx, value)) return false;
        if (in_recovery) m.recovery_steps++; /* redo during recovery only */
    }
    return true;
}
static bool queue_checkpoint(void) {
    in_checkpoint = true;
    m.checkpoints++;
    if (!write_fram(QUEUE_COMMIT, 1)) { in_checkpoint = false; return false; }
    if (!queue_apply()) { in_checkpoint = false; return false; }
    if (!write_fram(QUEUE_COMMIT, 0)) { in_checkpoint = false; return false; }
    bool ok = write_fram(QUEUE_COUNT, 0);
    in_checkpoint = false;
    return ok;
}
static bool queue_recover(void) {
    if (read_fram(QUEUE_COMMIT) == 1) {
        if (!queue_apply()) return false;
        if (!write_fram(QUEUE_COMMIT, 0)) return false;
    }
    if (queue_size() > 0 && !write_fram(QUEUE_COUNT, 0)) return false;
    for (int i = 0; i < N_SLOTS; ++i) sram[i] = read_fram(i);
    return true;
}

static bool recover_now(void) {
    in_recovery = true;
    bool ok = use_stack ? stack_recover() : queue_recover();
    in_recovery = false;
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 5 || argc > 6) {
        fprintf(stderr,"Usage: %s stack|queue K failure_denominator seed [log_capacity]\n", argv[0]);
        return 2;
    }
    use_stack = strcmp(argv[1], "stack") == 0;
    if (!use_stack && strcmp(argv[1], "queue") != 0) return 2;
    int K = atoi(argv[2]);
    fail_denominator = atoi(argv[3]);
    unsigned long seed = strtoul(argv[4], NULL, 10);
    log_capacity = argc == 6 ? atoi(argv[5]) : MAX_CAP;
    if (K < 1 || K > NUM_SAMPLES || fail_denominator < 0 ||
        log_capacity < N_SLOTS || log_capacity > MAX_CAP) {
        fprintf(stderr,"Invalid K, failure rate or capacity\n"); return 2;
    }
    init_simulation((uint32_t)seed);
    uint64_t guard = 0;
    while (sram[STEP] < NUM_SAMPLES || failed) {
        if (++guard > 20000000ULL) { fprintf(stderr,"Too many iterations\n"); return 3; }
        if (failed) {
            failed = false;
            if (!recover_now()) continue;
        }
        if (sram[STEP] >= NUM_SAMPLES) break;
        int current_log = use_stack ? stack_size() : queue_size();
        /* Full-log safety test: commit before another sample could overflow.
           Every sample needs at most 5 undo entries or exactly 5 redo entries. */
        if (current_log > log_capacity - N_SLOTS) {
            m.full_log_events++;
            if (!(use_stack ? stack_checkpoint() : queue_checkpoint())) continue;
            continue;
        }
        int old_step = sram[STEP];
        m.attempts++;
        if (!(use_stack ? stack_process_one() : queue_process_one())) continue;
        /* commit only once a complete sample is recorded */
        if (sram[STEP] != old_step + 1) { fprintf(stderr,"step mismatch\n"); return 3; }
        if (sram[STEP] % K == 0) {
            if (!(use_stack ? stack_checkpoint() : queue_checkpoint())) continue;
        }
    }
    /* The last partial interval must also be committed for a durable result. */
    if (use_stack) {
        if (stack_size() > 0) {
            while (true) {
                if (failed) { failed = false; if (!recover_now()) continue; }
                if (stack_checkpoint()) break;
            }
        }
    } else {
        if (queue_size() > 0) {
            while (true) {
                if (failed) { failed = false; if (!recover_now()) continue; }
                if (queue_checkpoint()) break;
            }
        }
    }
    int base = use_stack ? STACK_STATE : 0;
    int32_t step = read_fram(base + STEP), count = read_fram(base + COUNT);
    int32_t sum = read_fram(base + SUM), min = read_fram(base + MIN);
    int32_t max = read_fram(base + MAX);
    int pass = step == NUM_SAMPLES && count == 2000 && sum == 1026639 &&
               min == 7 && max == 1341 && sum / count == 513;
    /* CSV row: method,k,denominator,seed,pass,writes,reads,recovery_steps,
       max_log,extra_steps,failures,checkpoint_failures,full_log_events,checkpoints */
    printf("%s,%d,%d,%lu,%d,%llu,%llu,%llu,%d,%llu,%llu,%llu,%llu,%llu\n",
        use_stack ? "stack" : "queue",K,fail_denominator,seed,pass,
        (unsigned long long)m.writes,(unsigned long long)m.reads,
        (unsigned long long)m.recovery_steps,m.max_log,
        (unsigned long long)(m.attempts - NUM_SAMPLES),
        (unsigned long long)m.failures,(unsigned long long)m.checkpoint_failures,
        (unsigned long long)m.full_log_events,(unsigned long long)m.checkpoints);
    return pass ? 0 : 1;
}
