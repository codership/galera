// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "job_queue.h"

#define MAX_JOBS 8

static void init_worker(struct job_worker *worker, ushort id, ushort workers) {
    ushort i;
    worker->ident = IDENT_job_worker;
    worker->state = JOB_VOID;
    worker->ctx   = NULL;
    worker->id    = id;

    for (i=0; i<workers; i++) {
        worker->waiters[i] = 0;
    }

    gu_cond_init(&(worker->cond), NULL);
}

struct job_queue *job_queue_create(
    ushort max_workers, job_queue_conflict_fun conflict_test
) {
    struct job_queue *queue;
    ushort i;

    MAKE_OBJ(queue, job_queue);

    queue->active_workers = 0;
    queue->max_workers    = max_workers;
    queue->conflict_test  = conflict_test;

    gu_mutex_init(&queue->mutex, NULL);

    for (i=0; i<max_workers; i++) {
        init_worker(&(queue->jobs[i]), i, max_workers);
    }

    return queue;
}

int job_queue_destroy(struct job_queue *queue) {

    gu_free(queue);
    return WSDB_OK;
}


struct job_worker *job_queue_new_worker(struct job_queue *queue) {
    struct job_worker *worker = NULL;
    int i=0;

    CHECK_OBJ(queue, job_queue);
    gu_mutex_lock(&(queue->mutex));

    if (queue->active_workers == queue->max_workers) {
        gu_mutex_unlock(&(queue->mutex));
        return NULL;
    }
    
    while (i<queue->max_workers && !worker) {
        if (queue->jobs[i].state == JOB_VOID) {
            worker = &(queue->jobs[i]);
        }
        i++;
    }
    if (!worker) {
        gu_mutex_unlock(&(queue->mutex));
        return NULL;
    }

    queue->active_workers++;
    worker->state = JOB_IDLE;
    gu_mutex_unlock(&(queue->mutex));

    return (worker);
}
void job_queue_remove_worker(
    struct job_queue *queue, struct job_worker *worker
) {
    CHECK_OBJ(queue, job_queue);
    CHECK_OBJ(worker, job_worker);

    gu_mutex_lock(&(queue->mutex));

    assert(worker->state==JOB_IDLE);
    assert(worker->id <= queue->active_workers);

    worker->state = JOB_VOID;
    queue->active_workers--;
    gu_mutex_unlock(&(queue->mutex));
}

int job_queue_start_job(
    struct job_queue *queue, struct job_worker *worker, void *ctx
) {
    ushort i;
    CHECK_OBJ(queue, job_queue);
    CHECK_OBJ(worker, job_worker);

    if (worker->state == JOB_RUNNING) {
        gu_warn ("job %d  already running", worker->id);
        return WSDB_OK;
    }

    gu_mutex_lock(&(queue->mutex));

    /* check against all active jobs */
    for (i=0; i<queue->active_workers; i++) {
        if (queue->jobs[i].state == JOB_RUNNING && 
            queue->jobs[i].id != worker->id
        ) {
            if (queue->conflict_test(ctx, queue->jobs[i].ctx)) {
                queue->jobs[i].waiters[worker->id] = 1;
                gu_warn ("job %d  waiting for: %d", worker->id, i);
                gu_cond_wait(&queue->jobs[worker->id].cond, &queue->mutex);
                gu_warn ("job queue released: %d", worker->id);
                queue->jobs[i].waiters[worker->id] = 0;
            }
        }
    }
    worker->ctx   = ctx;
    worker->state = JOB_RUNNING;

    gu_debug("job: %d starting", worker->id);
    gu_mutex_unlock(&(queue->mutex));
    return WSDB_OK;
}

int job_queue_end_job(struct job_queue *queue, struct job_worker *worker
) {
    ushort i;
    CHECK_OBJ(queue, job_queue);
    CHECK_OBJ(worker, job_worker);

    //assert(worker == queue->jobs[worker->id]);

    gu_mutex_lock(&queue->mutex);
    for (i=0; i<queue->active_workers; i++) {
        if (queue->jobs[worker->id].waiters[i]) {
	  gu_warn ("job queue signal for: %d", i);
          gu_cond_signal(&queue->jobs[i].cond);
        }
    }
    queue->jobs[worker->id].state = JOB_IDLE;
    queue->jobs[worker->id].ctx   = NULL;
    
    gu_debug("job: %d complete", worker->id);
    gu_mutex_unlock(&(queue->mutex));

    return WSDB_OK;
}

