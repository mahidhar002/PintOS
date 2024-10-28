/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes the semaphore SEMA with a value of VALUE. A semaphore 
   is a non-negative integer with two atomic operations:

   - down (P): Wait until the value becomes positive and then decrement it.
   - up (V): Increment the value and wake one waiting thread, if any. */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down (P) operation on a semaphore. Waits for SEMA's value to become
   positive and then atomically decrements it.

   This function may sleep, and should not be called within an interrupt
   handler. It can be called with interrupts disabled, but if it sleeps,
   the next scheduled thread will re-enable interrupts. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_intr_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_intr_level = intr_disable ();
  struct thread *curr_thread = thread_current ();
  
  while (sema->value == 0) 
  {
    list_push_back(&sema->waiters, &curr_thread->elem);
    thread_block ();
  }

  sema->value--;
  intr_set_level (old_intr_level);
}

/* Down (P) operation on a semaphore, but returns immediately 
   if the semaphore's value is 0. Returns true if the semaphore 
   was decremented, false otherwise. This can be called from an 
   interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_intr_level;
  bool success;

  ASSERT (sema != NULL);

  old_intr_level = intr_disable ();
  if (sema->value > 0) 
  {
    sema->value--;
    success = true; 
  }
  else
    success = false;
  
  intr_set_level (old_intr_level);
  return success;
}

/* Up (V) operation on a semaphore. Increments SEMA's value
   and wakes one thread waiting on SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_intr_level;

  ASSERT (sema != NULL);

  old_intr_level = intr_disable ();
  
  if (!list_empty (&sema->waiters)) 
  {
    struct list_elem *max_priority_thread = list_max(&sema->waiters, thread_priority_compare, NULL);
    list_remove(max_priority_thread);
    thread_unblock(list_entry(max_priority_thread, struct thread, elem));
  }
  
  sema->value++;
  intr_set_level (old_intr_level);
  
  if (!intr_context())
    thread_yield();
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores: causes control to "ping-pong"
   between a pair of threads. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  
  for (i = 0; i < 10; i++) 
  {
    sema_up (&sema[0]);
    sema_down (&sema[1]);
  }

  printf ("done.\n");
}

/* Helper function for semaphore self-test. */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
  {
    sema_down (&sema[0]);
    sema_up (&sema[1]);
  }
}

/* Initializes a lock. A lock can only be held by one thread at 
   any given time. This implementation of a lock is not recursive, 
   meaning that a thread cannot acquire the same lock twice. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  lock->priority = PRI_MIN;
}

/* Helper function for recursive priority donation. */
static void
donate_priority_recursively(struct lock *lock, int priority)
{
  if (lock == NULL)
    return;
  
  lock->priority = lock->priority > priority ? lock->priority : priority;
  lock->holder->donation_priority = lock->holder->donation_priority > priority ? lock->holder->donation_priority : priority;
  
  donate_priority_recursively(lock->holder->thread_lock, priority);
}

/* Acquires LOCK, sleeping until it becomes available if necessary.
   The lock must not already be held by the current thread.

   This function may sleep, so it must not be called within an
   interrupt handler. It can be called with interrupts disabled. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  
  struct thread *current_thread = thread_current();
  enum intr_level curr_intr_level = intr_disable();
  
  int current_priority = (current_thread->donation_priority > current_thread->priority) ? current_thread->donation_priority : current_thread->priority;
  
  if (!lock_try_acquire(lock)) 
  {
    donate_priority_recursively(lock, current_priority);
    current_thread->thread_lock = lock;
    sema_down (&lock->semaphore);
    lock->holder = current_thread;
    current_thread->thread_lock = NULL;
    list_push_front(&current_thread->lock_list, &lock->lock_list_elem);
  }
  
  intr_set_level(curr_intr_level);
}

/* Tries to acquire LOCK. Returns true if successful or false otherwise.
   This function does not sleep, so it can be called within an interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *current_thread = thread_current();
  success = sema_try_down (&lock->semaphore);
  
  if (success) 
  {
    lock->holder = current_thread;
    list_push_front(&current_thread->lock_list, &lock->lock_list_elem);
  }
  
  return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This function cannot be called from an interrupt handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *current_thread = thread_current();
  enum intr_level curr_intr_level = intr_disable();

  current_thread->donation_priority = PRI_MIN;
  struct list *locks_held = &current_thread->lock_list;
  struct list_elem *e;
  
  for (e = list_begin (locks_held); e != list_end (locks_held); e = list_next (e))
  {
    struct lock *held_lock = list_entry(e, struct lock, lock_list_elem);
    
    if (lock == held_lock)
    {
      list_remove(e);
      lock->holder = NULL;
      lock->priority = PRI_MIN;
      continue;
    }
    
    current_thread->donation_priority = (current_thread->donation_priority > held_lock->priority) ? current_thread->donation_priority : held_lock->priority;
  }
  
  intr_set_level(curr_intr_level);
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false otherwise. */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
{
  struct list_elem elem;              /* List element. */
  struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);
  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled. 
   After being signaled, LOCK is reacquired. LOCK must be held 
   before calling this function. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_insert_ordered (&cond->waiters, &waiter.elem, &thread_priority_compare, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* Signals one thread waiting on COND (protected by LOCK) to wake up. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
  {
    list_sort (&cond->waiters, cond_priority_cmp, NULL);
    sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
  }

  if (!intr_context())
    thread_yield();
}

/* Wakes up all threads waiting on COND (protected by LOCK). */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

/* Helper function to sort the waiters on a condition variable based on priority. */
bool
cond_priority_cmp (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct semaphore_elem *sema_a = list_entry (a, struct semaphore_elem, elem);
  struct semaphore_elem *sema_b = list_entry (b, struct semaphore_elem, elem);
  
  return list_entry(list_front(&sema_a->semaphore.waiters), struct thread, elem)->priority >
      list_entry(list_front(&sema_b->semaphore.waiters), struct thread, elem)->priority;
}
