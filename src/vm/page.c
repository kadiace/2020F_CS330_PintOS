#include "page.h"
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

static unsigned vm_hash_func (const struct hash_elem *elem, void *aux UNUSED);
static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

void
vm_init (struct hash *vm)
{
  hash_init(vm, vm_hash_func, vm_less_func, NULL);
}

static unsigned
vm_hash_func (const struct hash_elem *elem, void *aux UNUSED)
{
  return hash_int(hash_entry (elem, struct spte, hash_elem)->vaddr);
}

static bool
vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  return hash_entry (a, struct spte, hash_elem)->vaddr
    < hash_entry (b, struct spte, hash_elem)->vaddr;
}

bool
insert_vm_entry (struct hash *vm_table, struct spte *vm_entry)
{
  if (hash_insert(vm_table, vm_entry) != NULL)
    return false;
  return true;
}

bool
delete_vm_entry (struct hash *vm_table, struct spte *vm_entry)
{
  if (hash_delete(vm_table, vm_entry) != NULL)
    return false;
  return true;
}

struct spte *
find_vm_entry (void *vaddr)
{
  /* Make new vm_entry, and */
  struct spte vm_entry;
  vm_entry.vaddr = pg_round_down(vaddr);
  struct hash_elem *elem = hash_find(&thread_current()->vm_table, &vm_entry.hash_elem);
  if (elem = NULL)
    return NULL;
  
  return hash_entry (elem, struct spte, hash_elem);
}

void
vm_destroy (struct hash *vm_table)
{
  hash_destroy(vm_table, NULL);
}
