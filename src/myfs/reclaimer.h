#pragma once

/**
 * @file reclaimer.h
 */

#include <stdbool.h>

typedef enum {
    RECLAIMER_LEVEL_OFF        = 0,     //<! Do not reclaim.
    RECLAIMER_LEVEL_OPTIMISTIC = 1,     //<! Reclaim whenever it's determined that nothing else is going on.
    RECLAIMER_LEVEL_AGGRESSIVE = 2      //<! Reclaim after every operation that can.
} reclaimer_level_t;

typedef enum {
    RECLAIMER_ACTION_GENERAL,            //<! An general action was taken.
    RECLAIMER_ACTION_DELETE              //<! Specifically, a delete action was taken.
} reclaimer_action_t;

void reclaimer_init();
void reclaimer_free();

bool reclaimer_start();
void reclaimer_stop();

void reclaimer_notify(reclaimer_action_t action);
