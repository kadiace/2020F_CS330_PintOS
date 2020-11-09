#include "swap.h"
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <bitmap.h>
#include "devices/block.h"
#include "vm/page.h"
#include "vm/frame.h"

static struct lock swap_lock;
struct bitmap *swap_table;

extern struct lock file_lock;

void
swap_init (size_t size)
{
  lock_init(&swap_lock);
  swap_table = bitmap_create(size);
}

void
swap_in (size_t swap_idx, void *kaddr)
{
  struct block *swap_block;
  swap_block = block_get_role (BLOCK_SWAP);
  lock_acquire(&file_lock);
  lock_acquire(&swap_lock);
  if (bitmap_test(swap_table, swap_idx) == false)
    ASSERT("Can not swap in a free block\n");

  bitmap_flip(swap_table, swap_idx);

  for (int i = 0; i < 8; i++)
  {
    block_read(swap_block, swap_idx + i, (uint8_t *) kaddr + i * BLOCK_SECTOR_SIZE);
  }
  bitmap_set_multiple (swap_table, swap_idx, 8, false);
  lock_release(&swap_lock);
  lock_release(&file_lock);
}

size_t
swap_out (void *kaddr)
{
  struct block *swap_block;
  swap_block = block_get_role (BLOCK_SWAP);
  lock_acquire(&file_lock);
  lock_acquire(&swap_lock);
  size_t free_idx = bitmap_scan_and_flip(swap_table, 0, 8, false);
  if (free_idx == BITMAP_ERROR)
    PANIC("No free index in swap block\n");
  for (int i = 0; i < 8; i++)
  {
    block_write(swap_block, free_idx + i, (uint8_t *) kaddr + i * BLOCK_SECTOR_SIZE);
  }
  lock_release(&swap_lock);
  lock_release(&file_lock);
  return free_idx;
}

