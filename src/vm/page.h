#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <list.h>
#include <hash.h>
#include "vm/frame.h"

#define SWAP_DISK   0
#define EXEC_FILE   1
#define MMAP_FILE   2
#define MEMORY      3

struct spte
{
  uint8_t type;                   /* Indicate page status such as SWAP_DISK, EXEC_FILE and MEMORY */
  void *vaddr;                    /* Virtual address that process may access */
  bool writable;                  /* If writable is true, the user process may modify the page. Otherwise, it is read-only.  */
  bool is_loaded;                 /* If this struct in memory, true, otherwise false */

  /* for lazy loading */
  struct file *file;              /* File that opened from block */
  size_t offset;                  /* Location of current file pointer */
  size_t read_bytes;              /* Read bytes that this spte read from file */
  size_t zero_bytes;              /* zero_bytes = PGSIZE - page_read_bytes */

  struct hash_elem elem;          /* Find spte by hash. */
  struct list_elem map_elem;      /* Element of map_file's list. */
  size_t swap_location;           /* Swap location. */
};

void spt_init (struct hash *spt);
bool insert_spte (struct hash *spt, struct spte *spte);
bool delete_spte (struct hash *spt, struct spte *spte);
struct spte *find_spte (void *vaddr);
void spt_destroy (struct hash *spt);
void check_valid_buffer (void *buffer, unsigned size, void *esp, bool is_writable);
void check_valid_string (void * string, void *esp);

struct map_file
{
  int map_id;
  struct file *file;
  struct list_elem elem;
  struct list spte_list;
};

#endif /* vm/page.h */