#include "frame.h"
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

static struct list frame_table;
static struct lock frame_lock;

struct list *
get_frame_table(void)
{
  return &frame_table;
}

void
frame_init()
{
  lock_init(&frame_lock);
  list_init(&frame_table);
}

struct fte *
alloc_fte (enum palloc_flags flag, struct spte *spte)
{
  struct fte *frame = (struct fte *)calloc(1, sizeof(struct fte));
  if (frame == NULL)
    return NULL;
  frame->spte = spte;
  frame->thread = thread_current();
  frame->kaddr = palloc_get_page(flag);

  /* Swap out victim while succeed it, and palloc_get_page() */
  while (frame->kaddr == NULL)
  {
    /* Find victim and swap out him. */
    struct fte *victim_frame = find_victim();
    struct spte *spte = victim_frame->spte;
    spte->type = SWAP_DISK;
    spte->is_loaded = false;

    /* Clear pagedir. */
    if (pagedir_is_accessed (victim_frame->thread->pagedir, spte->vaddr))
      pagedir_set_accessed (victim_frame->thread->pagedir, spte->vaddr, false);
    pagedir_clear_page (victim_frame->thread->pagedir, victim_frame->spte->vaddr);
    spte->swap_location = swap_out(victim_frame->kaddr);
    free_frame (victim_frame);

    /* Get page. */
    frame->kaddr = palloc_get_page(flag);
  }
  lock_acquire(&frame_lock);
  list_push_back(&frame_table, &frame->elem);
  lock_release(&frame_lock);
  return frame;
}

void
free_frame_perfect (struct fte* frame)
{
  /* Free frame and spte. */
  lock_acquire(&frame_lock);
  while(!delete_spte(&frame->thread->spt, frame->spte));
  list_remove(&frame->elem);
  free(frame->spte);
  palloc_free_page(frame->kaddr);
  free(frame);
  lock_release(&frame_lock);
}

void
free_frame (struct fte* frame)
{
  /* Free frame. */
  lock_acquire(&frame_lock);
  list_remove(&frame->elem);
  palloc_free_page(frame->kaddr);
  free(frame);
  lock_release(&frame_lock);
}

void
free_frame_table (struct thread* thread)
{
  /* Free frame table. */
  lock_acquire(&frame_lock);
  struct list_elem *base = list_begin(&frame_table);
  struct fte *base_frame;
  while (base == list_tail(&frame_table))
  {
    base_frame = list_entry(base, struct fte, elem);
    if (base_frame->thread == thread)
    {
      palloc_free_page(base_frame->kaddr);
      free(base_frame);
      base = list_remove(base);
    }
    else
      base = base->next;
  }
  lock_release(&frame_lock);
}

struct fte *
find_victim(void)
{
  /* Find victime with FIFO method, and return. */
  struct fte *victim;
  lock_acquire(&frame_lock);
  struct list_elem *evict_elem = list_pop_front(&frame_table);
  list_push_back(&frame_table, evict_elem);
  lock_release(&frame_lock);
  return list_entry(evict_elem, struct fte, elem);
}