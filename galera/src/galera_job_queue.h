// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef WSDB_JOB_QUEUE
#define WSDB_JOB_QUEUE

#include "galerautils.h"
#include "wsdb_api.h"

/* absolute maximum for workers */
#define MAX_WORKERS 8


/*!
 * Job queue abstraction
 * 
 * Job queue holds information of workers and jobs assigned for 
 * the workers. Each worker can execute one job at a time. 
 * 
 * Job queue is created and removed with job_queue_create() 
 * and job_queue_destroy(). Reference to job queue must be
 * passed to all subsequent job queue calls.
 * The create function requires a conflict detection function
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
 * The application call back is used for this conflict detection.
 * If there is no conflict, function returns immediately. If there
 * is a conflict, the function call blocks until all conflicting
 * jobs have ended.
 *
 * Worker ends a job by job_queue_end_job() call. This will signal
 * to release all workers, which are conflicting with the ending job.
 *
 * Job queue has a configuration for maximum concurrent job limit.
 * Queue will guarantee that at any given time this limit is not exceeded.
 * The concurrent job limit controls just number of active workers
 * (workers executing a job). There can be more registered (but idle) 
 * workers in the queue.
 */

/*
 * @brief checks if two jobs can run in parallel
 *
 * Function is passed two job context pointers.
 * The function is supposed to tell, if the first
 * job can run together with the second job.
 *
 * @param ctx1 context for job 1, this is tested 
 * @param ctx2 context for job 2, wich is already running
 * @return 1, if job1 can be started, 0 otherwise
 */
typedef int (*job_queue_conflict_fun)(void *, void *);

/*
 * WHY job_que_cmp_fun?
 *
 * Current parallel applying suffers from uneven lock granularity between
 * wsdb write sets and innodb applying. innodb needs sometimes larger lock set
 * than what is present in write set, and this results in BF deadloacking 
 * problems, that are hard to deal with.
 * So we can currently process only with one applying slave thread. However,
 * trx replaying model, requires local connection to run as BF applying worker
 * for replaying trx. In this mode, there will be two appliers running 
 * concurrenly and lock granularity issue can harm even one slave thread
 * configuration.
 * Two cope with this, we need to make sure that there is only one applying
 * thread active at any given time. We modified job_queue to control the
 * number of active appliers, and letting waiting appliers to enter in the
 * correct total order. job_queue_cmp_fun is used for letting the application
 * to tell which is the next job to start executing.
 * 
 */

/*
 * @brief compares the execution order of two jobs
 * Functions is passed two job context pointers.
 * The function is supposed to tell, if the first
 * job should run before the second job
 *
 * @param ctx1 context for job 1, this is tested 
 * @param ctx2 context for job 2, wich is already running
 * @return -1, 0, 1 
 */
typedef int (*job_queue_cmp_fun)(void *, void *);

struct job_id {
    unsigned short job_id;
};

enum job_state {
    JOB_VOID,
    JOB_RUNNING,
    JOB_IDLE,
    JOB_WAITING,
};

/*! @enum
 * JOB_SLAVE     = usual remote slave applying job
 * JOB_REPLAYING = replicated and certified local trx, which is replaying 
 *                 to earlier BF abort
 */
enum job_type {
    JOB_SLAVE,        //!< usual trx applied
    JOB_REPLAYING,    //!< BF aborted trx replaying
    JOB_TO_ISOLATION, //!< TO isolation
};

/*!
 * @struct job worker
 */
struct job_worker {
    char           ident;
    unsigned short id;                  //!< array index in job queue
    enum job_state state;               //!< current state
    void           *ctx;                //!< context pointer to application
    unsigned short waiters[MAX_WORKERS];//!< jobs waiting for this to complete
    gu_cond_t      cond;                //!< used together with queue mutex
    enum job_type  type;                //!< slave or replaying job 
};

#define IDENT_job_worker 'j'

/*!
 * @struct job queue
 */
struct job_queue {
    char                   ident;
    unsigned short         max_concurrent_workers; //!< limit for active workers
    unsigned short         registered_workers;     //!< # of workers in total
    unsigned short         active_workers;         //!< # of running jobs
    struct job_worker      jobs[MAX_WORKERS];      //!< all workers
    gu_mutex_t             mutex;
    job_queue_conflict_fun conflict_test;          //!< test for parallel job
    job_queue_cmp_fun      job_cmp_order;          //!< test for best job order
};

#define IDENT_job_queue 'J'


/*
 * @brief creates job array
 *
 * @param max_elems estimated max number of elems
 * @return pointer to the cache
 */
struct job_queue *job_queue_create(
    unsigned short max_workers, job_queue_conflict_fun, job_queue_cmp_fun);

/*
 * @brief destroys the job queue and releases all resources
 * @param queue the job queue to be destroyed
 * @return success code
 */
int job_queue_destroy(struct job_queue *queue);

/*
 * @brief create new worker
 * @param job_queue job queue, where worker is working
 */
struct job_worker *job_queue_new_worker(
    struct job_queue *queue, enum job_type type);

/*
 * @brief remove worker resource
 * @param job_queue job queue, where worker is working
 * @param worker worker to loose
 */
void job_queue_remove_worker(
    struct job_queue *queue, struct job_worker *worker
);

/*
 * @brief starts a new job for a worker
 * @param job_queue queue for the jobs
 * @param worker worker doing the job
 * @param ctx context pointer for conflict resolution
 * @return id for the new object
 */
int job_queue_start_job(
    struct job_queue *queue, struct job_worker *worker, void *ctx);

/*
 * @brief marks a job completed
 * @param job_queue queue fo rthe jobs
 * @param worker worker doing the job
 * @return context pointer
 */
void* job_queue_end_job(
    struct job_queue *queue, struct job_worker *worker);

#define _MAKE_OBJ(obj, def, size)                    \
{                                                    \
    obj = (struct def *) gu_malloc (size);           \
    if (!obj) {                                      \
        gu_error ("job queue internal error");       \
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
        gu_error ("job queue internal error");       \
        assert(0);                                   \
    }                                                \
}

#endif // WSDB_JOB_QUEUE
