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

bool synch_high_func (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
static bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);
static bool sema_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);
static bool lock_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      // Insert the threads in priority order
      ASSERT(list_begin(&sema->waiters) != NULL);
      list_insert_ordered(&sema->waiters, &thread_current ()->elem, &thread_less_func, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
  struct thread* t = NULL;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  sema->value++;
  if (!list_empty (&sema->waiters)){ 
    // Sort the lists in case the threads changed priority
    list_sort(&(sema->waiters), thread_less_func, NULL);

    // Unblock the thread holding the semaphore with the highest priority
    t = list_entry(list_pop_front(&sema->waiters), struct thread, elem);
    thread_unblock (t);
  }

  // See if the current thread needs to yield to the unblocked thread
   if(t != NULL && t->priority > thread_current()->priority) {
    thread_yield(); // this is causing alarm clock problems.
  }

  intr_set_level(old_level);

}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
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

/* Thread function used by sema_self_test(). */
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

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);

  // Initialize the lock at lowest priority first
  lock->priority = PRI_MIN;
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread* lock_holder = lock->holder;
  struct thread* t_cur = thread_current();
  struct lock *cur_lock = lock;

  //current thread wants the lock
  t_cur->lock_desired = lock;

  // First acquisition of lock case
  if(lock->holder == NULL) {
    lock->priority = t_cur->priority;
  }

  while(lock_holder != NULL && t_cur->priority > lock_holder->priority){
    thread_set_priority_donation(lock_holder, t_cur->priority, true);
    if(t_cur->priority > cur_lock->priority){
      cur_lock->priority = t_cur->priority;
    }

    if(lock_holder->lock_desired != NULL){
      cur_lock = lock_holder->lock_desired;
      lock_holder = cur_lock->holder;
    }else{
      break;
    }
  }

  sema_down (&lock->semaphore);
  lock->holder = thread_current();

  lock->holder->lock_desired = (struct lock*) NULL;
  list_insert_ordered(&(lock->holder->locks), &(lock->lockelem), lock_less_func, NULL);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  struct list_elem *next;
  struct lock *next_lock;

  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *t_cur = thread_current();
  lock->holder = NULL;
  sema_up (&lock->semaphore);

  list_remove(&(lock->lockelem));
  if(list_empty(&(t_cur->locks))){
    thread_set_priority_donation(t_cur, t_cur->original_priority, true);
  }else {
    list_sort(&(t_cur->locks), lock_less_func, NULL);
    next = list_front(&(t_cur->locks));
    next_lock = list_entry(next, struct lock, lockelem);
    thread_set_priority_donation(t_cur, next_lock->priority, true);
    }
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
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

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);

  // Assign the new semaphore with a priority
  waiter.semaphore.priority = thread_current()->priority;
  list_insert_ordered(&cond->waiters, &waiter.elem, &synch_high_func, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  struct semaphore* sema = NULL;

  /*if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);*/

  if (!list_empty (&cond->waiters)){ 
    // Sort the lists in case the threads changed priority
    list_sort(&(cond->waiters), sema_less_func, NULL);

    // Unblock the thread holding the semaphore with the highest priority
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
  }

  // See if the current thread needs to yield to the unblocked thread
  /*if(sema != NULL && sema->priority > thread_current()->priority) {
    thread_yield();
  }*/
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}


/*synch comparator function for insuring priority ordering from high to low*/
bool synch_high_func (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
  struct thread * ready_thread_1;
  struct thread * ready_thread_2;
  ready_thread_1 = list_entry (a, struct thread, elem);
  ready_thread_2 = list_entry (b, struct thread, elem);
  ASSERT(ready_thread_1 != NULL);
  ASSERT(ready_thread_2 != NULL);
  return ready_thread_1->priority > ready_thread_2->priority;
}

/* Helper function passed as a parameter to list functions that
 * tell it how to compare two elements of a thread list. */
static bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct thread *lthread, *rthread;
  ASSERT (l != NULL && r != NULL);
  lthread = list_entry(l, struct thread, elem);
  rthread = list_entry(r, struct thread, elem);
  return (lthread->priority >= rthread->priority);
}

/* Helper function passed as a parameter to list functions that
 * tell it how to compare two elements of a semaphore_elem list. */
static bool sema_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct semaphore_elem *lsema, *rsema;
  ASSERT (l != NULL && r != NULL);
  lsema = list_entry(l, struct semaphore_elem, elem);
  rsema = list_entry(r, struct semaphore_elem, elem);
  return (lsema->semaphore.priority > rsema->semaphore.priority);
}

static bool lock_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct lock *llock, *rlock;
  ASSERT (l != NULL && r != NULL);
  llock = list_entry(l, struct lock, lockelem);
  rlock = list_entry(r, struct lock, lockelem);
  return (llock->priority > rlock->priority);
}