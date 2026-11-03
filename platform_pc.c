
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "baseline.h"
#include "samples.h"

// SRAM is lost on power failure. FRAM survives.
int32_t sram[FRAM_SIZE] = {0};
int32_t fram[FRAM_SIZE] = {0};
uint32_t fram_write_count = 0;

static bool simulation_active = false;
static bool power_failed = false;
static uint32_t failure_count = 0;

// Simulate losing power. Call after a sample or a FRAM write.
bool trigger_power_failure_if_needed(void) {
    if (simulation_active && !power_failed &&
        (rand() % 100) < FAILURE_RATE_PERCENT) {
        memset(sram, 0, sizeof(sram));
        power_failed = true;
        failure_count++;
        return true;
    }
    return false;
}

void fram_write(uint16_t index, int32_t value) {
    if (power_failed || index >= FRAM_SIZE) return;
    fram[index] = value;
    fram_write_count++;
    trigger_power_failure_if_needed(); // Failure may happen DURING checkpoint.
}

int32_t fram_read(uint16_t index) {
    return index < FRAM_SIZE ? fram[index] : 0;
}

// Return the newest completed checkpoint, or -1 if none exists.
static int latest_slot(void) {
    bool a = (fram_read(SLOT0) == 1);
    bool b = (fram_read(SLOT1) == 1);
    if (!a && !b) return -1;
    if (!a) return SLOT1;
    if (!b) return SLOT0;
    return fram_read(SLOT0 + 1) >= fram_read(SLOT1 + 1)
           ? SLOT0 : SLOT1;
}

// Save in the OTHER slot so the previous checkpoint stays safe.
void baseline_checkpoint(void) {
    int old_slot = latest_slot();
    int new_slot = (old_slot == SLOT0) ? SLOT1 : SLOT0;

    fram_write(new_slot, 0); // Mark new slot incomplete.
    if (power_failed) return;

    for (int i = 0; i < 5; i++) {
        fram_write(new_slot + 1 + i, sram[i]);
        if (power_failed) return;
    }

    fram_write(new_slot, 1); // Mark complete LAST.
}

// Restore the last safe state after a failure.
void baseline_recover(void) {
    int slot = latest_slot();
    memset(sram, 0, sizeof(sram));

    if (slot < 0) {
        // No checkpoint yet: begin at sample 0.
        sram[MIN] = INT32_MAX;
        sram[MAX] = INT32_MIN;
        return;
    }

    for (int i = 0; i < 5; i++) {
        sram[i] = fram_read(slot + 1 + i);
    }
}

int main(void) {
    srand((unsigned int)time(NULL)); // New random sequence each run.
    baseline_recover();             // Initialize the starting state.
    simulation_active = true;

    while (sram[STEP] < NUM_SAMPLES || power_failed) {
        if (power_failed) {
            power_failed = false;
            baseline_recover();
            continue;
        }

        int16_t sample = samples[sram[STEP]];
        sram[SUM] += sample;
        sram[COUNT]++;
        if (sample < sram[MIN]) sram[MIN] = sample;
        if (sample > sram[MAX]) sram[MAX] = sample;
        sram[STEP]++;

        // Failures can happen after processing any sample.
        if (trigger_power_failure_if_needed()) continue;

        if (sram[STEP] % CHECKPOINT_INTERVAL == 0) {
            baseline_checkpoint();
            // A power failure inside baseline_checkpoint() is recovered
            // at the top of the loop, using the previous valid slot.
        }
    }

    simulation_active = false;
    printf("  ~~~~ Final Results ~~~~\n");
    printf("Count: %d\n", (int)sram[COUNT]);
    printf("Sum: %d\n", (int)sram[SUM]);
    printf("Min: %d\n", (int)sram[MIN]);
    printf("Max: %d\n", (int)sram[MAX]);
    printf("Average: %d\n", (int)(sram[SUM] / sram[COUNT]));
    printf("FRAM writes: %u\n", (unsigned)fram_write_count);
    printf("Power failures: %u\n", (unsigned)failure_count);
    return 0;
}
