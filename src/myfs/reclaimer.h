#pragma once

#define RECLAIMER_LEVEL_OFF       0  //<! Do not reclaim.
#define RECLAIMER_LEVEL_AGGRESIVE 1  //<! Reclaim after every operation that can.

void reclaimer_init();
void reclaimer_free();
