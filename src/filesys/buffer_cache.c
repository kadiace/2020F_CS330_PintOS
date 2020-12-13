#include "filesys/buffer_cache.h"
#include "threads/palloc.h"
#include <string.h>
#include <debug.h>

#define BUFFER_CACHE_ENTRIES 64

int clock_hand = 0;            																					/* Using in clock algorithm  */
static struct buffer_cache buffer_cache_table[BUFFER_CACHE_ENTRIES];		/* Manage buffer cache */
static char p_buffer_cache[BUFFER_CACHE_ENTRIES * BLOCK_SECTOR_SIZE];		/* Buffer that save real data */
struct lock bct_lock;


static void
clock_control(void)
{
  if (clock_hand < BUFFER_CACHE_ENTRIES - 1)
    clock_hand++;
  else
    clock_hand = 0;
}


void
buffer_cache_init (void)
{
	void *buffer = p_buffer_cache;
	for (int i = 0; i < BUFFER_CACHE_ENTRIES; i++)
	{
		memset(&buffer_cache_table[i], 0, sizeof(struct buffer_cache));
		buffer_cache_table[i].data = buffer + i * BLOCK_SECTOR_SIZE;
		lock_init(&buffer_cache_table[i].lock);
	}
	lock_init(&bct_lock);
}


void
buffer_cache_flush (struct buffer_cache *bc)
{
  if (bc->dirty)
  {
    bc->dirty = false;
    block_write(fs_device, bc->sector, bc->data);
  }
  bc->using = false;
  bc->chance = false;
}


void
buffer_cache_term(void)
{
  lock_acquire(&bct_lock);
  for (int i = 0; i < BUFFER_CACHE_ENTRIES; i++)
  {
    if (buffer_cache_table[i].using)
    {
      lock_acquire(&buffer_cache_table[i].lock);
      buffer_cache_flush(&buffer_cache_table[i]);
      lock_release(&buffer_cache_table[i].lock);
    }
  }
  lock_release(&bct_lock);
}


struct buffer_cache *
buffer_cache_lookup (block_sector_t sector_idx)
{
  lock_acquire(&bct_lock);
  for (int i = 0; i < BUFFER_CACHE_ENTRIES; i++)
  {
    if (buffer_cache_table[i].sector == sector_idx && buffer_cache_table[i].using)
    {
      lock_acquire(&buffer_cache_table[i].lock);
      lock_release(&bct_lock);
      return &buffer_cache_table[i];
    }
  }
  return NULL;
}


struct buffer_cache *
buffer_cache_victim(void)
{
  /* Moving clock hand, find victim. */
  while(buffer_cache_table[clock_hand].chance)
  {
    buffer_cache_table[clock_hand].chance = false;
    clock_control();
  }
  /* Evict victim. */
  int i = clock_hand;
  if (buffer_cache_table[clock_hand].using)
    buffer_cache_flush(&buffer_cache_table[clock_hand]);
  lock_acquire(&buffer_cache_table[i].lock);
  lock_release(&bct_lock);
  return &buffer_cache_table[i];
}


bool
buffer_cache_read(block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs)
{
  struct buffer_cache *bc = buffer_cache_lookup(sector_idx);
  if (bc == NULL)
  {
    bc = buffer_cache_victim();
    /* Caution! synch prob */
    bc->using = true;
    bc->dirty = false;
    bc->sector = sector_idx;
    block_read(fs_device, bc->sector, bc->data);
  }
  memcpy(buffer + bytes_read, bc->data + sector_ofs, chunk_size);
  bc->chance = true;
  lock_release(&bc->lock);
  return true;
}


bool
buffer_cache_write(block_sector_t sector_idx, void *buffer, off_t bytes_write, int chunk_size, int sector_ofs)
{
  struct buffer_cache *bc = buffer_cache_lookup(sector_idx);
  if (bc == NULL)
  {
    bc = buffer_cache_victim();
    bc->using = true;
    bc->sector = sector_idx;
    block_read(fs_device, bc->sector, bc->data);
  }
  memcpy(bc->data + sector_ofs, buffer + bytes_write, chunk_size);
  bc->chance = true;
  bc->dirty = true;
  lock_release(&bc->lock);
  return true;
}
