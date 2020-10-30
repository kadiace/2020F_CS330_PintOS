#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <list.h>
#include <hash.h>

#define SWAP_DISK   0
#define EXEC_FILE   1
#define MEMORY      2

struct spte
{
  uint8_t type;
  void *vaddr;
  bool writable;
  bool is_loaded;

  /* for lazy loading */
  struct file *file;
  size_t offset;
  size_t read_bytes;
  size_t zero_bytes;

  /* Find vm_enty by hash. */
  struct hash_elem hash_elem;
};

void vm_init (struct hash *vm);

#endif /* vm/page.h */