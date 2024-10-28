#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for detecting stack overflow in struct thread. */
#define THREAD_MAGIC 0xcd6abf4b

/* Lists for ready and all threads. */
static struct list ready_list;  /* Threads ready to run. */
static struct list all_list;    /* All threads in the system. */

/* Idle and initial threads. */
static struct thread *idle_thread;
static struct thread *initial_thread;

/* Lock for allocating thread IDs (TIDs). */
static struct lock tid_lock;

/* Kernel thread frame structure. */
struct kernel_thread_frame 
{
    void *eip;                  /* Return address. */
    thread_func *function;       /* Function to call. */
    void *aux;                   /* Auxiliary data for function. */
};

/* Thread scheduling statistics. */
static long long idle_ticks;    /* Time spent in idle threads. */
static long long kernel_ticks;  /* Time spent in kernel threads. */
static long long user_ticks;    /* Time spent in user programs. */

/* Scheduling constants and variables. */
#define TIME_SLICE 4            /* Timer ticks per time slice. */
static unsigned thread_ticks;   /* Timer ticks since last yield. */
bool thread_mlfqs;              /* Multi-level feedback queue scheduler flag. */

/* Function prototypes for internal thread operations. */
static void kernel_thread (thread_func *, void *aux);
static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system. */
void thread_init (void) 
{
    ASSERT (intr_get_level () == INTR_OFF);

    lock_init (&tid_lock);
    list_init (&ready_list);
    list_init (&all_list);

    /* Set up the initial thread structure. */
    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling and creates the idle thread. */
void thread_start (void) 
{
    struct semaphore idle_started;
    sema_init (&idle_started, 0);
    thread_create ("idle", PRI_MIN, idle, &idle_started);

    /* Enable preemptive scheduling. */
    intr_enable ();

    /* Wait for the idle thread to initialize. */
    sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick. */
void thread_tick (void) 
{
    struct thread *t = thread_current ();

    /* Update statistics. */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /* Enforce preemption if necessary. */
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return ();
}

/* Prints thread scheduling statistics. */
void thread_print_stats (void) 
{
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
            idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread with the given name, priority, and function. */
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux) 
{
    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;

    ASSERT (function != NULL);

    /* Allocate memory for the new thread. */
    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize the thread. */
    init_thread (t, name, priority);
    tid = t->tid = allocate_tid ();

    /* Set up stack frames for kernel_thread(). */
    kf = alloc_frame (t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    /* Set up stack frame for switch_entry(). */
    ef = alloc_frame (t, sizeof *ef);
    ef->eip = (void (*) (void)) kernel_thread;

    /* Set up stack frame for switch_threads(). */
    sf = alloc_frame (t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    /* Add the thread to the ready queue and yield if necessary. */
    thread_unblock (t);
    thread_yield();

    return tid;
}

/* Blocks the current thread. */
void thread_block (void) 
{
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);

    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

/* Unblocks a blocked thread, adding it to the ready queue. */
void thread_unblock (struct thread *t) 
{
    enum intr_level old_level;

    ASSERT (is_thread (t));

    old_level = intr_disable ();
    ASSERT (t->status == THREAD_BLOCKED);
    list_push_back (&ready_list, &t->elem);
    t->status = THREAD_READY;
    intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char * thread_name (void) 
{
    return thread_current ()->name;
}

/* Returns the running thread. */
struct thread *thread_current (void) 
{
    struct thread *t = running_thread ();
    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);
    return t;
}

/* Returns the current thread's ID. */
tid_t thread_tid (void) 
{
    return thread_current ()->tid;
}

/* Exits the current thread and never returns. */
void thread_exit (void) 
{
    ASSERT (!intr_context ());

#ifdef USERPROG
    process_exit ();
#endif

    intr_disable ();
    list_remove (&thread_current()->allelem);
    thread_current ()->status = THREAD_DYING;
    schedule ();
    NOT_REACHED ();
}

/* Compares wake-up times of threads for sleeping. */
bool wake_up_time_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) 
{
    struct thread *t1 = list_entry(a, struct thread, elem);
    struct thread *t2 = list_entry(b, struct thread, elem);
    return t1->wake_up_time < t2->wake_up_time;
}

/* Yields the CPU to another thread. */
void thread_yield (void) 
{
    struct thread *cur = thread_current ();
    enum intr_level old_level;
    
    ASSERT (!intr_context ());

    old_level = intr_disable ();
    if (cur != idle_thread) 
        list_push_back (&ready_list, &cur->elem);
    cur->status = THREAD_READY;
    schedule ();
    intr_set_level (old_level);
}

/* Applies a function to all threads. */
void thread_foreach (thread_action_func *func, void *aux)
{
    struct list_elem *e;

    ASSERT (intr_get_level () == INTR_OFF);

    for (e = list_begin (&all_list); e != list_end (&all_list);
         e = list_next (e))
    {
        struct thread *t = list_entry (e, struct thread, allelem);
        func (t, aux);
    }
}

/* Sets the current thread's priority. */
void thread_set_priority (int new_priority) 
{
    thread_current ()->priority = new_priority;
    thread_yield();
}

/* Returns the current thread's priority. */
int thread_get_priority (void) 
{
    struct thread* t = thread_current();
    return (t->donation_priority > t->priority) ? t->donation_priority : t->priority;
}

/* Not yet implemented. */
void thread_set_nice (int nice UNUSED) {}

/* Not yet implemented. */
int thread_get_nice (void) { return 0; }

/* Not yet implemented. */
int thread_get_load_avg (void) { return 0; }

/* Not yet implemented. */
int thread_get_recent_cpu (void) { return 0; }

/* The idle thread, which runs when no other threads are ready. */
static void idle (void *idle_started_ UNUSED) 
{
    struct semaphore *idle_started = idle_started_;
    idle_thread = thread_current ();
    sema_up (idle_started);

    for (;;) 
    {
        intr_disable ();
        thread_block ();
        asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Kernel thread function. */
static void kernel_thread (thread_func *function, void *aux) 
{
    ASSERT (function != NULL);

    intr_enable ();
    function (aux);
    thread_exit ();
}

/* Returns the running thread. */
struct thread *running_thread (void) 
{
    uint32_t *esp;
    asm ("mov %%esp, %0" : "=g" (esp));
    return pg_round_down (esp);
}

/* Verifies if the given pointer points to a valid thread. */
static bool is_thread (struct thread *t)
{
    return t != NULL && t->magic == THREAD_MAGIC;
}

/* Initializes a thread structure. */
static void init_thread (struct thread *t, const char *name, int priority)
{
    enum intr_level old_level;

    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;
    t->priority = priority;
    list_init(&t->lock_list);
    t->thread_lock = NULL;
    t->donation_priority = PRI_MIN;
    t->magic = THREAD_MAGIC;
    old_level = intr_disable ();
    list_push_back (&all_list, &t->allelem);
    intr_set_level (old_level);
}

/* Allocates a frame at the top of the thread's stack. */
static void *alloc_frame (struct thread *t, size_t size) 
{
    ASSERT (is_thread (t));
    ASSERT (size % sizeof (uint32_t) == 0);

    t->stack -= size;
    return t->stack;
}

/* Chooses and returns the next thread to run. */
static struct thread *next_thread_to_run (void) 
{
    if (list_empty (&ready_list))
        return idle_thread;
    else   
    {
        struct list_elem* max_thread = list_max(&ready_list, thread_priority_compare, NULL);
        list_remove(max_thread);
        return list_entry(max_thread, struct thread, elem);
    }
}

/* Completes a thread switch. */
void thread_schedule_tail (struct thread *prev)
{
    struct thread *cur = running_thread ();
    
    ASSERT (intr_get_level () == INTR_OFF);

    cur->status = THREAD_RUNNING;
    thread_ticks = 0;

#ifdef USERPROG
    process_activate ();
#endif

    if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
        ASSERT (prev != cur);
        palloc_free_page (prev);
    }
}

/* Schedules a new thread to run. */
static void schedule (void) 
{
    struct thread *cur = running_thread ();
    struct thread *next = next_thread_to_run ();
    struct thread *prev = NULL;

    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (cur->status != THREAD_RUNNING);
    ASSERT (is_thread (next));

    if (cur != next)
        prev = switch_threads (cur, next);
    thread_schedule_tail (prev);
}

/* Allocates a new TID for a thread. */
static tid_t allocate_tid (void) 
{
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire (&tid_lock);
    tid = next_tid++;
    lock_release (&tid_lock);

    return tid;
}

/* Offset of `stack' member within `struct thread', used by switch.S. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

bool
priority_compare(struct thread* t1, struct thread* t2)
{
  int t1_priority = t1->priority > t1->donation_priority ? t1->priority : t1->donation_priority;
  int t2_priority = t2->priority > t2->donation_priority ? t2->priority : t2->donation_priority;
  return t1_priority < t2_priority;
}

bool
thread_priority_compare(const struct list_elem *elem_a, const struct list_elem *elem_b, void *aux UNUSED)
{
  struct thread* t1 = list_entry(elem_a, struct thread, elem);
  struct thread* t2 = list_entry(elem_b, struct thread, elem);
  return priority_compare(t1, t2);
}

