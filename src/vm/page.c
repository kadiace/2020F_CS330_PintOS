#include "page.h"
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

static unsigned vm_hash_func (const struct hash_elem *base, void *aux UNUSED);
static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

void
vm_init (struct hash *vm)
{
  hash_init(vm, vm_hash_func, vm_less_func, NULL);
}

static unsigned
vm_hash_func (const struct hash_elem *base, void *aux UNUSED)
{
  return hash_int (hash_entry (base, struct spte, elem)->vaddr);
}

static bool
vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  return hash_entry (a, struct spte, elem)->vaddr
    < hash_entry (b, struct spte, elem)->vaddr;
}

bool
insert_vm_entry (struct hash *vm_table, struct spte *vm_entry)
{
  return hash_insert(vm_table, &vm_entry->elem) == NULL;
}

bool
delete_vm_entry (struct hash *vm_table, struct spte *vm_entry)
{
  if (hash_delete(vm_table, &vm_entry->elem) != NULL)
    return false;
  return true;
}

struct spte *
find_vm_entry (void *vaddr)
{
  /* Make new vm_entry, and */
  struct spte vm_entry;
  vm_entry.vaddr = pg_round_down(vaddr);
  struct hash_elem *base = hash_find(&thread_current()->vm_table, &vm_entry.elem);
  if (base == NULL)
  {
    return NULL;
  }
  return hash_entry (base, struct spte, elem);
}

void
vm_destroy (struct hash *vm_table)
{
  hash_destroy(vm_table, NULL);
}

struct spte *
check_valid_addr (void *addr, void *esp UNUSED)
{
  if(addr < (void *)0x08048000 || addr >= (void *)0xc0000000)
  {
    return NULL;
  }
  return find_vm_entry(addr);
}

void
check_valid_buffer (void *buffer, unsigned size, void *esp, bool is_writable)
{
  // printf("check_valid_buffer() : buffer addr %u, size %u\n", buffer, size);
  for (int i=0; i < size; i++)
  {
    struct spte *vm_entry = check_valid_addr (buffer + i, esp);
    if (vm_entry == NULL)
    {
      /* Check stack condition. If stack has problem, grow. */
      if (esp - buffer > 32 || 0xC0000000UL - (uint32_t)buffer > 8 * 1024 * 1024)
        exit(-1);
      /* Stack growth. */
      if (!stack_growth(buffer+i))
        exit(-1);
    }
    else if (is_writable && vm_entry->writable == false)
    {
      exit(-1);
    }
  }
}

void
check_valid_string (void * string, void *esp)
{
  struct spte *vm_entry = check_valid_addr (string, esp);
  if (vm_entry == NULL) 
  {
    exit(-1);
  }
}

bool
load_file (void *kaddr, struct spte *vme)
{
  if (file_read (vme->file, kaddr, vme->read_bytes) != (int) vme->read_bytes)
  {
    return false; 
  }
  memset (kaddr + vme->read_bytes, 0, vme->zero_bytes);
  return true;
}

