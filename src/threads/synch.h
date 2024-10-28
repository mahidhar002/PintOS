#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* Semaphore structure: A non-negative counter with a list of waiting threads. */
struct semaphore 
{
    unsigned value;             /* Current value of the semaphore. */
    struct list waiters;        /* List of threads waiting for the semaphore. */
};

/* Semaphore operations. */
void sema_init (struct semaphore *sema, unsigned value);
void sema_down (struct semaphore *sema);
bool sema_try_down (struct semaphore *sema);
void sema_up (struct semaphore *sema);
void sema_self_test (void);

/* Lock structure: A binary semaphore with an associated holder thread and priority. */
struct lock 
{
    struct thread *holder;          /* Thread holding the lock (for debugging). */
    struct semaphore semaphore;     /* Binary semaphore controlling access. */
    int priority;                   /* Maximum priority among threads holding the lock. */
    struct list_elem lock_list_elem; /* List element for the thread's lock list. */
};

/* Lock operations. */
void lock_init (struct lock *lock);
void lock_acquire (struct lock *lock);
bool lock_try_acquire (struct lock *lock);
void lock_release (struct lock *lock);
bool lock_held_by_current_thread (const struct lock *lock);

/* Condition variable structure: Used for signaling threads waiting for a condition. */
struct condition 
{
    struct list waiters;        /* List of semaphore elements representing waiting threads. */
};

/* Condition variable operations. */
void cond_init (struct condition *cond);
void cond_wait (struct condition *cond, struct lock *lock);
void cond_signal (struct condition *cond, struct lock *lock);
void cond_broadcast (struct condition *cond, struct lock *lock);

/* Function to sort condition variable waiters based on priority. */
bool cond_priority_cmp(const struct list_elem *a, const struct list_elem *b, void *aux);

/* Optimization barrier:
   Prevents the compiler from reordering operations across this barrier.
   Ensures memory operations occur in the expected order. */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
