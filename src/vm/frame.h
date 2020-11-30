#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <debug.h>
#include <list.h>
#include "threads/palloc.h"
#include "vm/page.h"

struct fte
{
  void *kaddr;                      /* Kernel address */
  struct spte *spte;                /* spte that connected with this frame */
  struct thread *thread;            /* Thread that contains this frame */

  struct list_elem elem;            /* List element. */
};

void frame_init (void);
struct fte * alloc_fte (enum palloc_flags flag, struct spte * spte);
struct list * get_frame_table (void);
void free_frame_perfect (struct fte* frame);
void free_frame (struct fte* frame);
void free_frame_table (struct thread* thread);
struct fte * find_victim (void);
#endif /* vm/frame.h */