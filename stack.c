#include "stack.h"

bool stack_is_empty(void) {
    return fram_read(TOP_ADDR) == 0;
}

bool stack_is_full(void) {
    return fram_read(TOP_ADDR) >= MAX_ENTRIES;
}

// Save the old value BEFORE changing any live state.
bool stack_push(int32_t index, int32_t old_value) {
    if (stack_is_full()) return false;
    int32_t top = fram_read(TOP_ADDR);
    int32_t pos = LOG_ADDR + 2 * top;
    if (!fram_write((uint16_t)pos, index)) return false;
    if (!fram_write((uint16_t)(pos + 1), old_value)) return false;
    return fram_write(TOP_ADDR, top + 1); // Entry is now valid.
}

// Undo last change. If a failure occurs, recovery can repeat this safely.
bool stack_pop(void) {
    if (stack_is_empty()) return false;
    int32_t top = fram_read(TOP_ADDR);
    int32_t pos = LOG_ADDR + 2 * (top - 1);
    int32_t index = fram_read((uint16_t)pos);
    int32_t old_value = fram_read((uint16_t)(pos + 1));
    if (index < STEP || index > COUNT) return false;
    if (!fram_write((uint16_t)(STATE_ADDR + index), old_value)) return false;
    return fram_write(TOP_ADDR, top - 1);
}

bool stack_clear(void) {
    return fram_write(TOP_ADDR, 0);
}

// Every change to a state value goes through the undo stack.
bool undo_update(int32_t index, int32_t new_value) {
    if (power_failed || index < STEP || index > COUNT) return false;
    int32_t old_value = fram_read((uint16_t)(STATE_ADDR + index));
    if (old_value == new_value) return true;
    if (!stack_push(index, old_value)) return false;
    if (!fram_write((uint16_t)(STATE_ADDR + index), new_value)) return false;
    sram[index] = new_value;
    return true;
}

// Commit the work since the previous checkpoint.
bool undo_checkpoint(void) {
    if (!fram_write(COMMIT_ADDR, 1)) return false;
    if (!stack_clear()) return false;
    return fram_write(COMMIT_ADDR, 0);
}

// Recover committed changes, or undo unfinished changes in reverse order.
bool undo_recover(void) {
    if (fram_read(COMMIT_ADDR) == 1) {
        if (!stack_clear()) return false;
        if (!fram_write(COMMIT_ADDR, 0)) return false;
    } else {
        while (!stack_is_empty()) {
            if (!stack_pop()) return false;
        }
    }
    // Restore the SRAM copy only after FRAM recovery is complete.
    for (int i = 0; i < 5; i++) {
        sram[i] = fram_read((uint16_t)(STATE_ADDR + i));
    }
    return true;
}
