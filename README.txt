PROJECT 1: INPUT FILES

samples.h  - NUM_SAMPLES vibration samples (int16_t), one per step.

Use: #include "samples.h"  then loop i = 0 .. NUM_SAMPLES-1 over samples[i].

Expected answer with NO power failures (use it to check your program):
  count = 2000
  sum   = 1026639
  min   = 7
  max   = 1341
  average (integer, sum / count) = 513

Every run WITH power failures must print exactly the same values.
Note: the sum is larger than 32,767, so store it in an int32_t.
