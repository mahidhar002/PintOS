#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* Thread life cycle states. */
enum thread_status
{
    THREAD_RUNNING,  /* Running thread. */
    THREAD_READY,    /* Ready to run but not currently running. */
    THREAD_BLOCKED,  /* Waiting for an event to trigger. */
    THREAD_DYING     /* About to be destroyed. */
};

/* Thread identifier type. Can be redefined to other types if needed. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)  /* Error value for tid_t. */

/* Thread priority definitions. */
#define PRI_MIN 0               /* Lowest priority. */
#define PRI_DEFAULT 31          /* Default priority. */
#define PRI_MAX 63              /* Highest priority. */

/* Kernel thread or user process.

   A thread's structure is stored at the bottom of its 4 kB page.
   The rest of the page is reserved for the thread's kernel stack,
   which grows downward from the top of the page.

   The structure must remain small to leave room for the stack.
   Large local variables or recursion can cause stack overflow,
   which can corrupt thread state and trigger assertion failures. */
struct thread
{
    /* Owned by thread.c. */
    tid_t tid;                         /* Thread identifier. */
    enum thread_status status;         /* Thread state. */
    char name[16];                     /* Name (for debugging purposes). */
    uint8_t *stack;                    /* Saved stack pointer. */
    int priority;                      /* Current thread priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    int64_t wake_up_time;              /* Time to wake up (used in sleeping). */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;             /* List element for ready list or semaphore wait list. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                 /* Page directory for user programs. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                    /* Detects stack overflow. */
    struct list lock_list;             /* List of locks acquired by the thread. */
    struct lock *thread_lock;          /* Pointer to the lock the thread is blocked on. */
    int donation_priority;             /* Donated priority from another thread. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

/* Thread initialization and starting functions. */
void thread_init (void);
void thread_start (void);

/* Thread tick and stats functions. */
void thread_tick (void);
void thread_print_stats (void);

/* Thread management and scheduling functions. */
typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);
void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);
void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Function to operate on all threads, passing auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

/* Thread priority manipulation functions. */
int thread_get_priority (void);
void thread_set_priority (int new_priority);

/* Functions related to nice value, recent CPU, and load average.
   These are used when the multi-level feedback queue scheduler is enabled. */
int thread_get_nice (void);
void thread_set_nice (int nice);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* Comparator functions for sleeping threads and priority scheduling. */
bool wake_up_time_less(const struct list_elem *a, const struct list_elem *b, void *aux);
bool thread_priority_compare(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
bool priority_compare(struct thread *first, struct thread *second);

#endif /* threads/thread.h */
