#ifndef STUDENT_B_QUEUE_H
#define STUDENT_B_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

/* Student B standalone queue/redo-log simulation. */
#define B_FRAM_SIZE 512
#define B_CHECKPOINT_INTERVAL 50
#define B_FAILURE_RATE_PERCENT 1
#define B_STATE_SIZE 5

#define B_STEP  0
#define B_SUM   1
#define B_MIN   2
#define B_MAX   3
#define B_COUNT 4

#define B_COMMIT_FLAG_ADDR 5
#define B_QUEUE_COUNT_ADDR 6
#define B_QUEUE_DATA_START 7
#define B_QUEUE_CAPACITY ((B_FRAM_SIZE - B_QUEUE_DATA_START) / 2)

void student_b_run(void);

#endif
