#ifndef STACK_H
#define STACK_H

#include <stdint.h>
#include <stdbool.h>

#define CHECKPOINT_INTERVAL 50
#define FRAM_SIZE 8192
#define FAILURE_CHANCE 20000 // One chance in 20,000 per FRAM write

// Positions of the five values in the saved state.
#define STEP 0
#define SUM 1
#define MIN 2
#define MAX 3
#define COUNT 4

// FRAM layout: commit, top, five state values, then stack entries.
#define COMMIT_ADDR 0
#define TOP_ADDR 1
#define STATE_ADDR 2
#define LOG_ADDR 7
#define MAX_ENTRIES ((FRAM_SIZE - LOG_ADDR) / 2)

extern int32_t fram[FRAM_SIZE];
extern int32_t sram[5];
extern uint32_t fram_write_count;
extern bool power_failed;

bool fram_write(uint16_t index, int32_t value);
int32_t fram_read(uint16_t index);

bool stack_is_empty(void);
bool stack_is_full(void);
bool stack_push(int32_t index, int32_t old_value);
bool stack_pop(void); // Undo the most recent change.
bool stack_clear(void);

bool undo_update(int32_t index, int32_t new_value);
bool undo_checkpoint(void);
bool undo_recover(void);

#endif
