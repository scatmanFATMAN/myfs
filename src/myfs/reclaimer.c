/**
 * @file reclaimer.c
 */

#include <string.h>
#include <time.h>
#include <pthread.h>
#include "../common/log.h"
#include "../common/config.h"
#include "../common/db.h"
#include "util.h"
#include "reclaimer.h"

#define MODULE "Reclaimer"

/** The number of seconds to wait before retry'ing a failed query. */
#define RECLAIMER_QUERY_RETRY_TIME 30

/** When running in optimistic mode, wait this many seconds for no actions before running. */
#define RECLAIMER_OPTIMISTIC_WAIT_TIME (60 * 30)

typedef struct {
    time_t last_action;          //<! When reclaimer was notified of the last action
    pthread_mutex_t lock;
} reclaimer_optimistic_t;

typedef struct {
    _Atomic bool run;                    //<! Whether or not reclaimer should run.
} reclaimer_aggressive_t;

typedef struct {
    db_t db;                              //<! The database connection.
    pthread_t thread;                     //<! The thread.
    _Atomic bool running;                 //<! If the thread is running or not.
    _Atomic reclaimer_level_t level;      //<! The level reclaimer is running at.
    reclaimer_optimistic_t optimistic;    //<! Data for optimistic reclaiming.
    reclaimer_aggressive_t aggressive;    //<! Data for aggressive reclaiming.
} reclaimer_t;

static reclaimer_t reclaimer;

static bool
reclaimer_should_run() {
    bool run = false;

    switch (reclaimer.level) {
        case RECLAIMER_LEVEL_OFF:
            break;
        case RECLAIMER_LEVEL_OPTIMISTIC:
            pthread_mutex_lock(&reclaimer.optimistic.lock);
            if (reclaimer.optimistic.last_action > 0) {
                run = time(NULL) - reclaimer.optimistic.last_action >= RECLAIMER_OPTIMISTIC_WAIT_TIME;
            }
            pthread_mutex_unlock(&reclaimer.optimistic.lock);
            break;
        case RECLAIMER_LEVEL_AGGRESSIVE:
            run = reclaimer.aggressive.run;
            break;
    }

    return run;
}

static void
reclaimer_reset() {
    switch (reclaimer.level) {
        case RECLAIMER_LEVEL_OFF:
            break;
        case RECLAIMER_LEVEL_OPTIMISTIC:
            pthread_mutex_lock(&reclaimer.optimistic.lock);
            reclaimer.optimistic.last_action = 0;
            pthread_mutex_unlock(&reclaimer.optimistic.lock);
            break;
        case RECLAIMER_LEVEL_AGGRESSIVE:
            reclaimer.aggressive.run = false;
            break;
    }
}

static void *
reclaimer_process(void *user_data) {
    time_t next_try = 0;
    bool should_run;
    MYSQL_RES *res;

    while (reclaimer.running) {
        //First, make sure una query should even run.
        should_run = reclaimer_should_run();
        if (!should_run) {
            util_sleep_ms(100);
            continue;
        }

        //If a previous query failed, pause until it's ready to retry.
        if (next_try > 0) {
            if (next_try > time(NULL)) {
                util_sleep_ms(10);
                continue;
            }
        }

        next_try = 0;

        //Run the query to reclaim disk space.
        //OPTIMIZE TABLES returns a result set so it MUST be free'd otherwise an error will occur on the next query.
        res = db_selectf(&reclaimer.db, "OPTIMIZE TABLE `file_data`,`file`", 33);
        if (res == NULL) {
            log_err(MODULE, "Error running query: Trying again in %d seconds: %s", RECLAIMER_QUERY_RETRY_TIME, db_error(&reclaimer.db));
            next_try = time(NULL) + RECLAIMER_QUERY_RETRY_TIME;
            continue;
        }

        mysql_free_result(res);
        reclaimer_reset();
    }

    return NULL;
}

void
reclaimer_init() {
    memset(&reclaimer, 0, sizeof(reclaimer));

    db_init(&reclaimer.db);
    pthread_mutex_init(&reclaimer.optimistic.lock, NULL);
}

void
reclaimer_free() {
    db_free(&reclaimer.db);
    pthread_mutex_destroy(&reclaimer.optimistic.lock);
}

bool
reclaimer_start() {
    bool success;
    int ret;

    reclaimer.level = config_get_int("reclaimer_level");

    if (reclaimer.level == RECLAIMER_LEVEL_OFF) {
        log_info(MODULE, "Reclaimer is off");
        return true;
    }

    log_info(MODULE, "Starting");

    //Connect to the database.
    success = db_connect(&reclaimer.db, config_get("mariadb_host"), config_get("mariadb_user"), config_get("mariadb_password"), config_get("mariadb_database"), config_get_uint("mariadb_port"));
    if (!success) {
        log_err(MODULE, "Error connecting to MariaDB: %s", db_error(&reclaimer.db));
        return false;
    }

    //Start the thread.
    reclaimer.running = true;
    ret = pthread_create(&reclaimer.thread, NULL, reclaimer_process, NULL);
    if (ret != 0) {
        reclaimer.running = false;
        log_err(MODULE, "Error starting thread: %s", strerror(ret));
    }

    return reclaimer.running;
}

void
reclaimer_stop() {
    //TODO: There is a still a race condition between the check and setting to false. This should probably just protected by a mutex.
    if (reclaimer.running) {
        reclaimer.running = false;
        log_info(MODULE, "Stopping");

        pthread_join(reclaimer.thread, NULL);
    }

    db_disconnect(&reclaimer.db);
}

void
reclaimer_notify(reclaimer_action_t action) {
    switch (reclaimer.level) {
        case RECLAIMER_LEVEL_OFF:
            break;
        case RECLAIMER_LEVEL_OPTIMISTIC:
            pthread_mutex_lock(&reclaimer.optimistic.lock);
            reclaimer.optimistic.last_action = time(NULL);
            pthread_mutex_unlock(&reclaimer.optimistic.lock);
            break;
        case RECLAIMER_LEVEL_AGGRESSIVE:
            if (action == RECLAIMER_ACTION_DELETE) {
                reclaimer.aggressive.run = true;
            }
            break;
    }
}
