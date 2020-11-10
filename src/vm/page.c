#include "page.h"
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

static unsigned spt_hash_func (const struct hash_elem *base, void *aux UNUSED);
static bool spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

void
spt_init (struct hash *spt)
{
  hash_init(spt, spt_hash_func, spt_less_func, NULL);
}

static unsigned
spt_hash_func (const struct hash_elem *base, void *aux UNUSED)
{
  return hash_int (hash_entry (base, struct spte, elem)->vaddr);
}

static bool
spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  return hash_entry (a, struct spte, elem)->vaddr
    < hash_entry (b, struct spte, elem)->vaddr;
}

bool
insert_spte (struct hash *spt, struct spte *spte)
{
  return hash_insert(spt, &spte->elem) == NULL;
}

bool
delete_spte (struct hash *spt, struct spte *spte)
{
  if (hash_delete(spt, &spte->elem) != NULL)
    return false;
  return true;
}

struct spte *
find_spte (void *vaddr)
{
  /* Find spte by vaddr. */
  struct spte spte;
  spte.vaddr = pg_round_down(vaddr);
  struct hash_elem *base = hash_find(&thread_current()->spt, &spte.elem);
  if (base == NULL)
  {
    return NULL;
  }
  return hash_entry (base, struct spte, elem);
}

void
spt_destroy (struct hash *spt)
{
  hash_destroy(spt, NULL);
}

struct spte *
check_valid_addr (void *addr, void *esp UNUSED)
{
  if(addr < (void *)0x08048000 || addr >= (void *)0xc0000000)
  {
    return NULL;
  }
  return find_spte(addr);
}

void
check_valid_buffer (void *buffer, unsigned size, void *esp, bool is_writable)
{
  for (int i=0; i < size; i++)
  {
    struct spte *spte = check_valid_addr (buffer + i, esp);
    if (spte == NULL)
    {
      /* Check stack condition. If stack has problem, grow. */
      if (esp - buffer > 32 || 0xC0000000UL - (uint32_t)buffer > 8 * 1024 * 1024)
        exit(-1);
      /* Stack growth. */
      if (!stack_growth(buffer+i))
        exit(-1);
    }
    else if (is_writable && spte->writable == false)
    {
      exit(-1);
    }
  }
}

void
check_valid_string (void * string, void *esp)
{
  struct spte *spte = check_valid_addr (string, esp);
  if (spte == NULL) 
  {
    exit(-1);
  }
}

bool
load_file (void *kaddr, struct spte *spte)
{
  if (file_read (spte->file, kaddr, spte->read_bytes) != (int) spte->read_bytes)
  {
    return false; 
  }
  memset (kaddr + spte->read_bytes, 0, spte->zero_bytes);
  return true;
}

