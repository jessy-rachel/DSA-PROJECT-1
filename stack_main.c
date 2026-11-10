#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "stack.h"
#include "samples.h"

int32_t fram[FRAM_SIZE];
int32_t sram[5];
uint32_t fram_write_count = 0;
bool power_failed = false;
static uint32_t failure_count = 0;
static bool simulation_active = false;

int32_t fram_read(uint16_t index) {
    return (index < FRAM_SIZE) ? fram[index] : 0;
}

// One FRAM write is atomic: power may fail immediately AFTER it.
bool fram_write(uint16_t index, int32_t value) {
    if (power_failed || index >= FRAM_SIZE) return false;
    fram[index] = value;
    fram_write_count++;
    if (simulation_active && rand() % FAILURE_CHANCE == 0) {
        power_failed = true;
        memset(sram, 0, sizeof(sram)); // SRAM loses its data.
        failure_count++;
        return false;
    }
    return true;
}

// This represents a fresh device and is not charged as checkpoint writes.
static void initialize(void) {
    memset(fram, 0, sizeof(fram));
    memset(sram, 0, sizeof(sram));
    sram[MIN] = INT32_MAX;
    sram[MAX] = INT32_MIN;
    fram[STATE_ADDR + MIN] = INT32_MAX;
    fram[STATE_ADDR + MAX] = INT32_MIN;
    fram_write_count = 0;
    failure_count = 0;
    power_failed = false;
    simulation_active = true;
}

// One vibration sample: log old values BEFORE writing new values.
static bool process_sample(void) {
    int32_t step = sram[STEP];
    int32_t sample = samples[step];
    if (!undo_update(SUM, sram[SUM] + sample)) return false;
    if (!undo_update(COUNT, sram[COUNT] + 1)) return false;
    if (sample < sram[MIN]) {
        if (!undo_update(MIN, sample)) return false;
    }
    if (sample > sram[MAX]) {
        if (!undo_update(MAX, sample)) return false;
    }
    return undo_update(STEP, step + 1); // Update step LAST.
}

static bool run_one(unsigned seed, bool show_output) {
    srand(seed);
    initialize();
    while (sram[STEP] < NUM_SAMPLES || power_failed) {
        if (power_failed) {
            power_failed = false; // Simulate power returning.
            if (!undo_recover()) continue; // Recovery may itself be interrupted.
        }
        if (sram[STEP] >= NUM_SAMPLES) break;
        if (!process_sample()) continue;
        if (sram[STEP] % CHECKPOINT_INTERVAL == 0) {
            if (!undo_checkpoint()) continue;
        }
    }
    simulation_active = false;
    int32_t count = fram_read(STATE_ADDR + COUNT);
    int32_t sum = fram_read(STATE_ADDR + SUM);
    int32_t min = fram_read(STATE_ADDR + MIN);
    int32_t max = fram_read(STATE_ADDR + MAX);
    bool correct = count == 2000 && sum == 1026639 && min == 7 && max == 1341;
    if (show_output) {
        printf("Count: %d\nSum: %d\nMin: %d\nMax: %d\n", count, sum, min, max);
        printf("Average: %d\n", count ? sum / count : 0);
        printf("FRAM writes: %u\nPower failures: %u\n", fram_write_count, failure_count);
        printf("Result: %s\n", correct ? "PASS" : "FAIL");
    }
    return correct;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        for (unsigned seed = 1; seed <= 1000; seed++) {
            if (!run_one(seed, false)) {
                printf("FAIL at seed %u\n", seed);
                return 1;
            }
        }
        printf("PASS: All 1000 runs produced the correct answer.\n");
    } else if (!run_one(42, true)) {
        return 1;
    }
    return 0;
