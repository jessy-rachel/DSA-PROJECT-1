#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "baseline.h"
#include "samples.h"

/*
 * Dirty-flag checkpoint implementation.
 * Build this file INSTEAD OF platform_pc.c (do not compile both: both define main).
 *
 * FRAM layout:
 *   0..4   data bank 0: STEP, SUM, MIN, MAX, COUNT
 *   5..9   data bank 1: STEP, SUM, MIN, MAX, COUNT
 *   10..11 manifest 0: valid flag, packed STEP + bank-selection mask
 *   12..13 manifest 1: valid flag, packed STEP + bank-selection mask
 *
 * Each variable has two FRAM copies. A checkpoint writes a changed variable
 * to its inactive copy, then commits a new manifest LAST. If power fails
 * before the manifest commit, the old manifest still selects the old values.
 */

int32_t sram[FRAM_SIZE] = {0};
int32_t fram[FRAM_SIZE] = {0};
uint32_t fram_write_count = 0;

#define STATE_COUNT 5
#define DATA_BANK0 0
#define DATA_BANK1 5
#define MANIFEST0 10
#define MANIFEST1 12
#define MANIFEST_VALID 0
#define MANIFEST_DATA 1
#define MASK_BITS 5
#define MASK_VALUE 31

static bool dirty[STATE_COUNT] = {true, true, true, true, true};
static bool simulation_active = false;
static bool power_failed = false;
static uint32_t failure_count = 0;

static void clear_dirty_flags(void) {
    for (int i = 0; i < STATE_COUNT; i++) dirty[i] = false;
}

static void mark_dirty(int index) {
    if (index >= 0 && index < STATE_COUNT) dirty[index] = true;
}

bool trigger_power_failure_if_needed(void) {
    if (simulation_active && !power_failed &&
        (rand() % 100) < FAILURE_RATE_PERCENT) {
        memset(sram, 0, sizeof(sram));
        clear_dirty_flags();
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
    trigger_power_failure_if_needed();
}

int32_t fram_read(uint16_t index) {
    return index < FRAM_SIZE ? fram[index] : 0;
}

static int latest_manifest(void) {
    bool a = (fram_read(MANIFEST0 + MANIFEST_VALID) == 1);
    bool b = (fram_read(MANIFEST1 + MANIFEST_VALID) == 1);

    if (!a && !b) return -1;
    if (!a) return MANIFEST1;
    if (!b) return MANIFEST0;

    int32_t data_a = fram_read(MANIFEST0 + MANIFEST_DATA);
    int32_t data_b = fram_read(MANIFEST1 + MANIFEST_DATA);
    return (data_a >> MASK_BITS) >= (data_b >> MASK_BITS)
           ? MANIFEST0 : MANIFEST1;
}

static int data_index(int variable, int bank) {
    return (bank == 0 ? DATA_BANK0 : DATA_BANK1) + variable;
}

/* Save only changed state values. The manifest is the atomic commit record. */
static void dirty_checkpoint(void) {
    int old_manifest = latest_manifest();
    int new_manifest = (old_manifest == MANIFEST0) ? MANIFEST1 : MANIFEST0;
    int32_t old_data = (old_manifest < 0) ? 0 :
                       fram_read(old_manifest + MANIFEST_DATA);
    int old_mask = (int)(old_data & MASK_VALUE);
    int new_mask = old_mask;

    for (int i = 0; i < STATE_COUNT; i++) {
        if (dirty[i]) {
            int old_bank = (old_mask >> i) & 1;
            int new_bank = 1 - old_bank;
            fram_write((uint16_t)data_index(i, new_bank), sram[i]);
            if (power_failed) return;

            if (new_bank == 1) new_mask |= (1 << i);
            else new_mask &= ~(1 << i);
        }
    }

    /* Prepare the alternate manifest. It is not valid until the last write. */
    fram_write((uint16_t)(new_manifest + MANIFEST_VALID), 0);
    if (power_failed) return;

    /* STEP and the five bank-selection bits fit together in one int32_t. */
    int32_t manifest_data = (sram[STEP] << MASK_BITS) | new_mask;
    fram_write((uint16_t)(new_manifest + MANIFEST_DATA), manifest_data);
    if (power_failed) return;

    fram_write((uint16_t)(new_manifest + MANIFEST_VALID), 1);
    if (power_failed) return;

    clear_dirty_flags();
}

static void dirty_recover(void) {
    int manifest = latest_manifest();
    memset(sram, 0, sizeof(sram));
    clear_dirty_flags();

    if (manifest < 0) {
        sram[MIN] = INT32_MAX;
        sram[MAX] = INT32_MIN;
        /* No checkpoint exists yet, so the first checkpoint stores all fields. */
        for (int i = 0; i < STATE_COUNT; i++) dirty[i] = true;
        return;
    }

    int32_t manifest_data = fram_read(manifest + MANIFEST_DATA);
    int mask = (int)(manifest_data & MASK_VALUE);
    for (int i = 0; i < STATE_COUNT; i++) {
        int bank = (mask >> i) & 1;
        sram[i] = fram_read((uint16_t)data_index(i, bank));
    }
}

int main(void) {
    srand((unsigned int)time(NULL));
    dirty_recover();
    simulation_active = true;

    while (sram[STEP] < NUM_SAMPLES || power_failed) {
        if (power_failed) {
            power_failed = false;
            dirty_recover();
            continue;
        }

        int16_t sample = samples[sram[STEP]];

        sram[SUM] += sample;
        mark_dirty(SUM);

        sram[COUNT]++;
        mark_dirty(COUNT);

        if (sample < sram[MIN]) {
            sram[MIN] = sample;
            mark_dirty(MIN);
        }

        if (sample > sram[MAX]) {
            sram[MAX] = sample;
            mark_dirty(MAX);
        }

        sram[STEP]++;
        mark_dirty(STEP);

        if (trigger_power_failure_if_needed()) continue;

        if (sram[STEP] % CHECKPOINT_INTERVAL == 0) {
            dirty_checkpoint();
        }
    }

    simulation_active = false;
    printf("  ~~~~ Dirty-Flag Results ~~~~\n");
    printf("Count: %d\n", (int)sram[COUNT]);
    printf("Sum: %d\n", (int)sram[SUM]);
