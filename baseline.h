
#ifndef BASELINE_H
#define BASELINE_H

#include <stdint.h>
#include <stdbool.h>

#define FRAM_SIZE 64
#define CHECKPOINT_INTERVAL 50
#define FAILURE_RATE_PERCENT 1

// Five working values stored in SRAM.
#define STEP  0
#define SUM   1
#define MIN   2
#define MAX   3
#define COUNT 4

// Each FRAM checkpoint has: valid flag + five state values.
#define SLOT_SIZE 6
#define SLOT0 0
#define SLOT1 6

extern int32_t sram[FRAM_SIZE];
extern int32_t fram[FRAM_SIZE];
extern uint32_t fram_write_count;

void fram_write(uint16_t index, int32_t value);
int32_t fram_read(uint16_t index);
bool trigger_power_failure_if_needed(void);
void baseline_checkpoint(void);
void baseline_recover(void);

#endif
