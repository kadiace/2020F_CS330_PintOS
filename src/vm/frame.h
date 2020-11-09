#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <debug.h>
#include <list.h>
#include "vm/page.h"

struct fte
{
  void *kaddr;
  struct spte *vme;
  struct thread *thread;

  struct list_elem elem;
};

void frame_init(void);
// struct fte * alloc_frame (enum palloc_flags flag, struct spte * vm_entry);
struct list * get_frame_table (void);
void free_frame_perfect (struct fte* frame);
void free_frame (struct fte* frame);
void free_frame_table (struct thread* thread);
struct fte * find_victim (void);
#endif /* vm/frame.h */