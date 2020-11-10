#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <list.h>
#include <hash.h>

#define SWAP_DISK   0
#define EXEC_FILE   1
#define MEMORY      2
#define STACK       3

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
  struct hash_elem elem;

  /* Swap location. */
  size_t swap_location;
};

void spt_init (struct hash *spt);
bool insert_spte (struct hash *spt, struct spte *spte);
bool delete_spte (struct hash *spt, struct spte *spte);
struct spte *find_spte (void *vaddr);
void spt_destroy (struct hash *spt);
void check_valid_buffer (void *buffer, unsigned size, void *esp, bool is_writable);
void check_valid_string (void * string, void *esp);
bool load_file (void *kaddr, struct spte *spte);

#endif /* vm/page.h */