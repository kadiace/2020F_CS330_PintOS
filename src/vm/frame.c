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
struct lock frame_lock;
extern struct lock file_lock;

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
    struct spte *victim_spte = victim_frame->spte;
    victim_spte->type = SWAP_DISK;

    /* Clear pagedir. */
    if (pagedir_is_accessed(victim_frame->thread->pagedir, victim_spte->vaddr))
      pagedir_set_accessed(victim_frame->thread->pagedir, victim_spte->vaddr, false);
    if (pagedir_is_dirty(victim_frame->thread->pagedir, victim_spte->vaddr))
      pagedir_set_dirty(victim_frame->thread->pagedir, victim_spte->vaddr, false);
    pagedir_clear_page(victim_frame->thread->pagedir, victim_frame->spte->vaddr);
    victim_spte->swap_location = swap_out(victim_frame->kaddr);
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
  if (frame->spte->type == MMAP_FILE && pagedir_is_dirty(frame->thread->pagedir, frame->spte->vaddr))
  {
    lock_acquire(&file_lock);
    file_write_at(frame->spte->file, frame->spte->vaddr, frame->spte->read_bytes, frame->spte->offset);
    lock_release(&file_lock);
  }
  /* Clear pagedir. */
  if (pagedir_is_accessed(frame->thread->pagedir, frame->spte->vaddr))
    pagedir_set_accessed(frame->thread->pagedir, frame->spte->vaddr, false);
  pagedir_clear_page(frame->thread->pagedir, frame->spte->vaddr);

  /* Free frame and spte. */
  lock_acquire(&frame_lock);
  delete_spte(&frame->thread->spt, frame->spte);
  list_remove(&frame->elem);
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
  struct list_elem * temp;
  struct fte *base_frame;
  while (base != list_tail(&frame_table))
  {
    temp = base->next;
    base_frame = list_entry(base, struct fte, elem);
    if (base_frame->thread == thread)
    {
      lock_release(&frame_lock);
      free_frame_perfect(base_frame);
      lock_acquire(&frame_lock);
    }
    base = temp;
  }
  lock_release(&frame_lock);
}

struct fte *
find_victim(void)
{
  /* Find victime with FIFO method, and return. */
  struct fte *victim;
  lock_acquire(&frame_lock);
  lock_acquire(&file_lock);
  struct list_elem *evict_elem = list_pop_front(&frame_table);

  /* Except mmap file. */
  while (list_entry(evict_elem, struct fte, elem)->spte->type == MMAP_FILE)
  {
    list_push_back(&frame_table, evict_elem);
    evict_elem = list_pop_front(&frame_table);
  }
  lock_release(&file_lock);
  lock_release(&frame_lock);
  return list_entry(evict_elem, struct fte, elem);
}