// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef WSDB_JOB_QUEUE
#define WSDB_JOB_QUEUE

#include "galerautils.h"
#include "wsdb_api.h"

#define MAX_JOBS 8

/*!
 * Job queue abstraction
 * 
 * Job queue holds information of workers and jobs
 * assigned for the workers. Each worker can execute
 * one job at a time. 
 * 
 * Job queue is created and removed with job_queue_create() 
 * and job_queue_destroy(). Reference to job queue must be
 * passed to all subsequent job queue calls.
 * The create function requires a conflict resolution function
 * handle. Application must implement this function to determine
 * if two given jobs can be executed parallelly.
 *
 * Workers register themselves with job_queue_new_worker()
 * method. They receive worker handle, which must be passed to
 * all subsequent job queue calls.
 * 
 * Worker starts a job with job_queue_start_job() call. Job queue
 * finds all jobs, which are running at the moment, and verifies
 * with each of them, if the new job can be started parallelly. 
 * The application call back is used for this conflict resolution.
 * If there is no conflict, function returns immediately. If there
 * is a conflict, the function call blocks until all conflicting
 * jobs have ended.
 *
 * Worker ends a job by job_queue_end_job() call. This will signal
 * all workers, which are conflicting with the ending job to continue.
 */

/*
 * @brief checks if two jobs can run in parallel
 * Functions is passed two job context pointers.
 * The function is supposed to tell, if the first
 * job can run together with the second job.
 *
 * @param ctx1 context for job 1, this is tested 
 * @param ctx2 context for job 2, wich is already running
 * @return 1, if job1 can be started, 0 otherwise
 */
typedef int (*job_queue_conflict_fun)(void *, void *);

struct job_id {
    ushort    job_id;
};

enum job_state {
    JOB_VOID,
    JOB_RUNNING,
    JOB_COMPLETED
};


/*!
 * @struct job queue
 */
struct job_worker {
    char ident;
    ushort id;                  //!< array index in job queue
    enum job_state state;     //!< current state
    void *ctx;                //!< context pointer to application structure
    ushort waiters[MAX_JOBS]; //!< jobs waiting for this to complete
    gu_cond_t cond;           //!< used together with queue mutex
};

#define IDENT_job_worker 'j'

struct job_queue {
    char                   ident;
    ushort                 max_workers;
    ushort                 active_workers;
    struct job_worker      jobs[MAX_JOBS];
    gu_mutex_t             mutex;
    job_queue_conflict_fun conflict_test;
};

#define IDENT_job_queue 'J'


/*
 * @brief creates job array
 *
 * @param max_elems estimated max number of elems
 * @return pointer to the cache
 */
struct job_queue *job_queue_create(ushort max_workers, job_queue_conflict_fun);

/*
 * @brief @fixme: 
 * @param id @fixme:
 * @return success code
 */
int job_queue_destroy(struct job_queue *queue);

/*
 * @brief create new worker
 * @param job_queue job queue, where worker is working
 */
struct job_worker *job_queue_new_worker(struct job_queue *queue);

/*
 * @brief starts a new job for a worker
 * @param job_queue queue fo rthe jobs
 * @param worker worker doing the job
 * @param ctx context pointer for conflict resolution
 * @return id for the new object
 */
int job_queue_start_job(
    struct job_queue *queue, struct job_worker *worker, void *ctx);

/*
 * @brief marks a job complete
 * @param job_queue queue fo rthe jobs
 * @param worker worker doing the job
 * @return id for the new object
 */
int job_queue_end_job(
    struct job_queue *queue, struct job_worker *worker);

#define _MAKE_OBJ(obj, def, size)                    \
{                                                    \
    obj = (struct def *) gu_malloc (size);           \
    if (!obj) {                                      \
        gu_error ("internal error");                 \
        assert(0);                                   \
    }                                                \
    obj->ident = IDENT_##def;                        \
}
#define MAKE_OBJ(obj, def)                           \
{                                                    \
    _MAKE_OBJ(obj, def, sizeof(struct def));         \
}
#define MAKE_OBJ_SIZE(obj, def, extra)               \
{                                                    \
    _MAKE_OBJ(obj, def, sizeof(struct def) + extra); \
}
    

#define CHECK_OBJ(obj, def)                          \
{                                                    \
    if (!obj || obj->ident != IDENT_##def) {         \
        gu_error ("internal error");                 \
        assert(0);                                   \
    }                                                \
}

#endif // WSDB_JOB_QUEUE
