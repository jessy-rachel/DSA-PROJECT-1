#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "student_b_queue.h"
#include "samples.h"

/* All names are Student-B-prefixed or file-local, so this program can sit
   beside the shared project without changing or colliding with it. */
static int32_t b_sram[B_FRAM_SIZE];
static int32_t b_fram[B_FRAM_SIZE];
static uint32_t b_fram_write_count;
static uint32_t b_failure_count;
static bool b_simulation_active;
static bool b_power_failed;
static uint16_t b_queue_head;

static bool b_trigger_power_failure(void)
{
    if (b_simulation_active && !b_power_failed &&
        (rand() % 100) < B_FAILURE_RATE_PERCENT) {
        memset(b_sram, 0, sizeof(b_sram));
        b_power_failed = true;
        b_failure_count++;
        return true;
    }
    return false;
}

static void b_fram_write(uint16_t address, int32_t value)
{
    if (b_power_failed || address >= B_FRAM_SIZE) return;
    b_fram[address] = value;
    b_fram_write_count++;
    (void)b_trigger_power_failure();
}

static int32_t b_fram_read(uint16_t address)
{
    return address < B_FRAM_SIZE ? b_fram[address] : 0;
}

static bool b_queue_enqueue_batch(const uint16_t *indices,
                                  const int32_t *values, uint16_t n)
{
    uint32_t count = (uint32_t)b_fram_read(B_QUEUE_COUNT_ADDR);
    if (b_power_failed || n == 0 || count + n > B_QUEUE_CAPACITY) return false;

    /* Publish the count only after every index/value pair is written. */
    for (uint16_t i = 0; i < n; ++i) {
        if (indices[i] >= B_STATE_SIZE) return false;
        uint16_t slot = (uint16_t)((count + i) % B_QUEUE_CAPACITY);
        uint16_t address = (uint16_t)(B_QUEUE_DATA_START + 2U * slot);
        b_fram_write(address, (int32_t)indices[i]);
        if (b_power_failed) return false;
        b_fram_write((uint16_t)(address + 1U), values[i]);
        if (b_power_failed) return false;
    }
    b_fram_write(B_QUEUE_COUNT_ADDR, (int32_t)(count + n));
    return !b_power_failed;
}

static bool b_queue_dequeue(uint16_t *index, int32_t *value)
{
    uint32_t count = (uint32_t)b_fram_read(B_QUEUE_COUNT_ADDR);
    if (b_power_failed || b_queue_head >= count) return false;

    uint16_t slot = (uint16_t)(b_queue_head % B_QUEUE_CAPACITY);
    uint16_t address = (uint16_t)(B_QUEUE_DATA_START + 2U * slot);
    int32_t stored_index = b_fram_read(address);
    if (stored_index < 0 || stored_index >= B_STATE_SIZE) return false;

    *index = (uint16_t)stored_index;
    *value = b_fram_read((uint16_t)(address + 1U));
    b_queue_head++;
    return true;
}

static void b_apply_queue(void)
{
    uint16_t index;
    int32_t value;
    b_queue_head = 0;
    while (b_queue_dequeue(&index, &value)) {
        b_fram_write(index, value);
        if (b_power_failed) return;
    }
}

static void b_checkpoint(void)
{
    if (b_power_failed) return;

    /* Commit intent is durable before replay begins. Replaying absolute
       assignments is safe if power fails part-way through this operation. */
    b_fram_write(B_COMMIT_FLAG_ADDR, 1);
    if (b_power_failed) return;

    b_apply_queue();
    if (b_power_failed) return;

    b_fram_write(B_COMMIT_FLAG_ADDR, 0);
    if (b_power_failed) return;

    b_queue_head = 0;
    b_fram_write(B_QUEUE_COUNT_ADDR, 0);
}

static void b_recover(void)
{
    b_queue_head = 0;

    if (b_fram_read(B_COMMIT_FLAG_ADDR) == 1) {
        b_apply_queue();
        if (b_power_failed) return;
        b_fram_write(B_COMMIT_FLAG_ADDR, 0);
        if (b_power_failed) return;
    }

    /* With no commit in progress, pending entries are uncommitted work. */
    if (b_fram_read(B_QUEUE_COUNT_ADDR) != 0) {
        b_fram_write(B_QUEUE_COUNT_ADDR, 0);
        if (b_power_failed) return;
    }

    for (uint16_t i = 0; i < B_STATE_SIZE; ++i)
        b_sram[i] = b_fram_read(i);
}

static bool b_queue_current_state(void)
{
    const uint16_t indices[B_STATE_SIZE] = {
        B_STEP, B_SUM, B_MIN, B_MAX, B_COUNT
    };
    const int32_t values[B_STATE_SIZE] = {
        b_sram[B_STEP], b_sram[B_SUM], b_sram[B_MIN],
        b_sram[B_MAX], b_sram[B_COUNT]
    };
    return b_queue_enqueue_batch(indices, values, B_STATE_SIZE);
}

void student_b_run(void)
{
    memset(b_sram, 0, sizeof(b_sram));
    memset(b_fram, 0, sizeof(b_fram));
    b_fram_write_count = 0;
    b_failure_count = 0;
    b_queue_head = 0;
    b_power_failed = false;
    b_simulation_active = false;

    b_sram[B_MIN] = INT32_MAX;
    b_sram[B_MAX] = INT32_MIN;
    /* Initial durable state. */
    for (uint16_t i = 0; i < B_STATE_SIZE; ++i)
        b_fram[i] = b_sram[i];

    srand((unsigned int)time(NULL));
    b_simulation_active = true;

    while (b_sram[B_STEP] < NUM_SAMPLES || b_power_failed) {
        if (b_power_failed) {
            b_power_failed = false;
            b_recover();
            continue;
        }

        int16_t sample = samples[b_sram[B_STEP]];
        b_sram[B_SUM] += sample;
        b_sram[B_COUNT]++;
        if (sample < b_sram[B_MIN]) b_sram[B_MIN] = sample;
        if (sample > b_sram[B_MAX]) b_sram[B_MAX] = sample;
        b_sram[B_STEP]++;

        if (!b_queue_current_state()) {
            if (b_power_failed) continue;
            fprintf(stderr, "Queue capacity/error; stopping safely.\n");
            return;
        }
        if (b_power_failed) continue;

        if (b_sram[B_STEP] % B_CHECKPOINT_INTERVAL == 0) {
            b_checkpoint();
        }
    }

    /* Commit the final partial interval (if any). */
    if (b_fram_read(B_QUEUE_COUNT_ADDR) != 0) {
        b_checkpoint();
        while (b_power_failed) {
            b_power_failed = false;
            b_recover();
            if (!b_power_failed && b_fram_read(B_QUEUE_COUNT_ADDR) != 0)
                b_checkpoint();
        }
    }

    b_simulation_active = false;
    printf("\nStudent B - Week 2: Circular Queue / Redo Log\n");
    printf("Actual recovered results:\n");
    printf("Count: %d\n", (int)b_sram[B_COUNT]);
    printf("Sum: %d\n", (int)b_sram[B_SUM]);
    printf("Min: %d\n", (int)b_sram[B_MIN]);
    printf("Max: %d\n", (int)b_sram[B_MAX]);
    printf("Average: %d\n", (int)(b_sram[B_SUM] / b_sram[B_COUNT]));
    printf("FRAM writes: %u\n", (unsigned)b_fram_write_count);
    printf("Power failures: %u\n", (unsigned)b_failure_count);

    if (b_sram[B_COUNT] == 2000 && b_sram[B_SUM] == 1026639 &&
        b_sram[B_MIN] == 7 && b_sram[B_MAX] == 1341 &&
        b_sram[B_SUM] / b_sram[B_COUNT] == 513) {
        printf("PASS: Results match the expected sample results.\n");
    } else {
        printf("FAIL: Results do not match the expected sample results.\n");
    }
}

int main(void)
{
    student_b_run();
    return 0;
}
