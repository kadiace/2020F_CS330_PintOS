#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <debug.h>
#include <list.h>
#include "vm/page.h"

void swap_init (size_t size);
void swap_in (size_t swap_idx, void *kaddr);
size_t swap_out (void *kaddr);

#endif /* vm/swap.h */