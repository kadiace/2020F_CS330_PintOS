#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/synch.h"

struct buffer_cache
{
  struct inode *inode;                /* Inode. */
  block_sector_t sector;              /* Point sector if filedisk. */
  bool using;                         /* Whether this buffer cache is using or not. */
  bool dirty;                         /* Check bc is dirty. */
  bool chance;                        /* Using at clock algorithm. If chance is 1, bc can survive once. */
  void *data;                         /* Buffer that saved real data. */

  struct lock lock;                   /* Acquire this when accessing buffer cache. */
};

void buffer_cache_init(void);
void buffer_cache_flush(struct buffer_cache *bc);
void buffer_cache_term(void);
struct buffer_cache *buffer_cache_lookup(block_sector_t sector_idx);
struct buffer_cache *buffer_cache_victim(void);
bool buffer_cache_read(block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs);
bool buffer_cache_write(block_sector_t sector_idx, void *buffer, off_t bytes_write, int chunk_size, int sector_ofs);

#endif /* filesys/buffer_cache.h */