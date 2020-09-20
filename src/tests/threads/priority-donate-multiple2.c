/* The main thread acquires locks A and B, then it creates three
   higher-priority threads.  The first two of these threads block
   acquiring one of the locks and thus donate their priority to
   the main thread.  The main thread releases the locks in turn
   and relinquishes its donated priorities, allowing the third thread
   to run.

   In this test, the main thread releases the locks in a different
   order compared to priority-donate-multiple.c.
   
   Written by Godmar Back <gback@cs.vt.edu>. 
   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by Matt Franklin <startled@leland.stanford.edu>,
   Greg Hutchins <gmh@leland.stanford.edu>, Yu Ping Hu
   <yph@cs.stanford.edu>.  Modified by arens. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func a_thread_func;
static thread_func b_thread_func;
static thread_func c_thread_func;

void
test_priority_donate_multiple2 (void) 
{
  struct lock a, b;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);
  lock_init (&a);
  lock_init (&b);
  a.name = 1;
  b.name = 2;
  lock_acquire (&a);
  lock_acquire (&b);

  // msg("holder tid is %d", a.holder->tid);
  // msg("holder tid is %d", b.holder->tid);
  // msg ("a holder priority is %d", a.holder->priority);
  // msg ("b holder priority is %d", b.holder->priority);
  // msg ("a origin priority is %d", a.holder->origin_priority);
  // msg ("b origin priority is %d", b.holder->origin_priority);
  // msg ("a donate list size is %d", list_size(&a.holder->donated));
  // msg ("b donate list size is %d", list_size(&b.holder->donated));
  // msg ("a numlock is %d", a.holder->num_lock);
  // msg ("b numlock is %d", b.holder->num_lock);

  thread_create ("a", PRI_DEFAULT + 3, a_thread_func, &a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 3, thread_get_priority ());
  // msg("holder tid is %d", a.holder->tid);
  // msg("holder tid is %d", b.holder->tid);
  // msg ("a holder priority is %d", a.holder->priority);
  // msg ("b holder priority is %d", b.holder->priority);
  // msg ("a origin priority is %d", a.holder->origin_priority);
  // msg ("b origin priority is %d", b.holder->origin_priority);
  // msg ("a donate list size is %d", list_size(&a.holder->donated));
  // msg ("b donate list size is %d", list_size(&b.holder->donated));
  // msg ("a numlock is %d", a.holder->num_lock);
  // msg ("b numlock is %d", b.holder->num_lock);

  thread_create ("c", PRI_DEFAULT + 1, c_thread_func, NULL);

  thread_create ("b", PRI_DEFAULT + 5, b_thread_func, &b);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 5, thread_get_priority ());
  // msg("holder tid is %d", a.holder->tid);
  // msg("holder tid is %d", b.holder->tid);
  // msg ("a holder priority is %d", a.holder->priority);
  // msg ("b holder priority is %d", b.holder->priority);
  // msg ("a origin priority is %d", a.holder->origin_priority);
  // msg ("b origin priority is %d", b.holder->origin_priority);
  // msg ("a donate list size is %d", list_size(&a.holder->donated));
  // msg ("b donate list size is %d", list_size(&b.holder->donated));
  // msg ("a numlock is %d", a.holder->num_lock);
  // msg ("b numlock is %d", b.holder->num_lock);

  lock_release (&a);
  msg ("a numlock is %d", b.holder->num_lock);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 5, thread_get_priority ());

  lock_release (&b);
  msg ("Threads b, a, c should have just finished, in that order.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
}

static void
a_thread_func (void *lock_) 
{
  struct lock *lock = lock_;
  msg ("a start");
  lock_acquire (lock);
  msg ("Thread a acquired lock a.");
  lock_release (lock);
  msg ("Thread a finished.");
}

static void
b_thread_func (void *lock_) 
{
  struct lock *lock = lock_;
  msg ("b start");
  lock_acquire (lock);
  msg ("Thread b acquired lock b.");
  lock_release (lock);
  msg ("Thread b finished.");
}

static void
c_thread_func (void *a_ UNUSED) 
{
  msg ("Thread c finished.");
}
