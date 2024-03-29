#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "vm/frame.h"


static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

void
push_args_to_stack (char** argv, int argc, void **esp)
{
  int total_len = 0;
  int len;

  /* Push argv[argc-1] ~ argv[0]. */
  for (int i = argc-1;i >= 0;i--)
  {
    /* Save arg[n-1]. */
    len = strlen(argv[i]);
    *esp -=len + 1;
    total_len += len + 1;
    strlcpy(*esp, argv[i], len + 1);
    argv[i] = *esp;
  }

  /* Push word align. */
  if (total_len % 4 != 0)
    *esp -= 4 - (total_len % 4);

  /* push NULL. */
  *esp -= 4;
  **(uint32_t **)esp = 0;

  /* Push address of argv[argc-1] ~ argv[0]. */
  for (int i = argc-1;i >= 0;i--)
  {
    *esp -= 4;
    **(uint32_t **)esp = argv[i];
  }

  /* Push address of argv. */
  *esp -= 4;
  **(uint32_t **)esp = *esp + 4;

  /* Push argc. */
  *esp -= 4;
  **(uint32_t **)esp = argc;

  /* Push return address .*/
  *esp -= 4;
  **(uint32_t **)esp = 0;

  /* Check stack by hex_dump(). */
  // printf("hexdump!\n");
  // uintptr_t ofs = (uintptr_t)*esp;
  // uintptr_t byte_size = 0xc0000000-ofs;
  // hex_dump(ofs, *esp, byte_size, true);
}

int
seperate_fn (char* fn_copy, char** fn_token)
{
  int i = 0;
  char* ptr;
  char* next_ptr;

  /* Get full command by split fn_copy by '\n'  */
  ptr = strtok_r(fn_copy, "\n", &next_ptr);
  while (ptr != NULL)
  {
    /* Split whole sentence by ' '. */
    ptr = strtok_r(ptr, " ", &next_ptr);
    if (ptr == NULL)
      break;

    /* Put argument in stack. */
    *(fn_token+i) = ptr;
    ptr = next_ptr;
    i++;
  }

  /* Return argc. */
  return i;
}

void
get_command (char *command, char *file_name)
{
  /* Allocate and copy. */
  strlcpy(command, file_name, strlen(file_name) + 1);

  /* Put NULL at last address. */
  int i;
  for (i = 0; command[i] != NULL && command[i] != ' ';i++);
  command[i] = NULL;
}

struct thread *
find_child(struct thread *parent, tid_t tid)
{
  /* Check whole child list of parent. */
  struct list_elem *base = list_begin(&thread_current()->child_list);
  while (base != list_tail(&parent->child_list))
  {
    /* If parent have child that have same tid with parameter, return. */
    struct thread* child_thread = list_entry(base, struct thread, child_elem);
    if (child_thread->tid == tid)
    {
      return child_thread;
    }
    base = base->next;
  }
  return NULL;
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  char command[256];
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Get command from file_name. */
  get_command(command, file_name);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (command, PRI_DEFAULT, start_process, fn_copy);
  /* If child has problem, return -1. */
  if (tid == TID_ERROR)
  {
    palloc_free_page (fn_copy);
    return TID_ERROR;
  }  
  /* Get child process, and check child has problem. */
  struct thread *child = find_child(thread_current(), tid);
  if (child == NULL) {
    palloc_free_page (fn_copy);
    return -1;
  }

  /* Load sema down. */
  sema_down(&child->load_sema);

  /* If child fail to load memory, exit. */
  if (!child->load_success)
  {
    palloc_free_page(fn_copy);
    return -1;
  }
  /* Free fn_copy and return child's tid to wait for him. */
  palloc_free_page (fn_copy);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  char *command;
  char *next_ptr;
  struct intr_frame if_;
  bool success;
  /* Seperate file_name and put first argument in thread_create(). */
  int argc = seperate_fn(file_name, &command);
  char ** argv = &command;

  /* Initialize spt. */
  spt_init (&thread_current()->spt);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (command, &if_.eip, &if_.esp);

  /* Update T/F : child is success to load. */
  thread_current()->load_success = success;

  /* Update arguments in stack. */
  if (success) {push_args_to_stack(argv, argc, &if_.esp);}

  /* Load_sema up. */
  sema_up(&thread_current()->load_sema);

  /* If load failed, quit. */
  if (!success)
    exit(-1);
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  /* Find child of current thread. If not, return -1. */
  struct thread *parent = thread_current();
  struct thread *child = find_child(parent, child_tid);
  if (child == NULL)
    return -1;
  /* Change sema before remove child, and return child's exit status. */
  sema_down(&child->wait_sema);
  int status = child->exit_status;
  list_remove(&child->child_elem);
  sema_up(&child->exit_sema);

  /* Exit in. */
  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Clean mmap_file */
  munmap(CLOSE_ALL);

  /* Free spt. */
  free_frame_table(thread_current());
  spt_destroy(&thread_current()->spt);

  /* remove files which is used by current process */
  struct file **table = cur->fd_table;
  for (int i = 2; i < 128; i++)
  {
    if (cur->fd_table[i] != NULL)
      close(i);
  }
  free(cur->fd_table);

  /* If child process killed due to an exception, clean up the parent-child relationship. */
  struct thread* child;
  struct list_elem *base = list_begin(&cur->child_list);
  while (base != list_tail(&cur->child_list))
  {
    child = list_entry(base, struct thread, child_elem);
    sema_up(&child->exit_sema);
    base = list_remove(&child->child_elem);
  }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  dir_close (cur->dir);

  /* wait_sema up, exit_sema down. */
  sema_up(&cur->wait_sema);
  sema_down(&cur->exit_sema);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

extern struct lock file_lock;

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  lock_acquire(&file_lock);

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      lock_release(&file_lock);
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  
  lock_release(&file_lock);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Make spte, insert at spt. */
      struct spte *spte = (struct spte *)calloc (1, sizeof (struct spte));
      if (spte == NULL)
        return false;
      memset (spte, 0, sizeof(struct spte));
      spte->type = EXEC_FILE;
      spte->vaddr = upage;
      spte->writable = writable;
      spte->is_loaded = false;
      spte->file = file_reopen(file);
      spte->offset = ofs;
      spte->read_bytes = page_read_bytes;
      spte->zero_bytes = page_zero_bytes;
      insert_spte (&thread_current()->spt, spte);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += page_read_bytes;
    }
    
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  /* Set vaddr, spte, fte. */
  uint8_t *upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
  struct spte *spte = calloc(1, sizeof (struct spte));
  struct fte *frame = alloc_fte ((PAL_USER | PAL_ZERO), spte);

  /* Insert stack at physical memory. */
  if (spte == NULL)
  {
    free_frame_perfect (frame);
    return false;
  }
  if (frame != NULL) 
  {
    if (install_page (upage, frame->kaddr, true))
    {
      *esp = PHYS_BASE;

      spte->type = MEMORY;
      spte->vaddr = upage;
      spte->writable = true;
      spte->is_loaded = true;

      if (!insert_spte (&thread_current()->spt, spte))
      {
        free_frame_perfect (frame);
        return false;
      }
    }
    else
    {
      free_frame_perfect (frame);
      return false;
    }
  }
  else
  {
    free_frame_perfect (frame);
    return false;
  }
  return true;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

bool
load_file (struct fte *frame, struct spte *spte)
{
  /* Get a page of memory. */
  if (spte->read_bytes != 0)
  {
    if (file_read_at (spte->file, frame->kaddr, spte->read_bytes, spte->offset) != (int) spte->read_bytes)
      return false;
  }

  /* Memory set 0. */
  memset (frame->kaddr + spte->read_bytes, 0, spte->zero_bytes);

  /* Install page. */
  if (!install_page(spte->vaddr, frame->kaddr, spte->writable))
    return false;
  return true;
}

bool
handle_pf(struct spte *spte)
{
  /* Make fte. */
  struct fte *frame = alloc_fte (PAL_USER, spte);
  if (frame == NULL)
    return false;

  /* Divide situation by sp type. */
  uint8_t type = frame->spte->type;
  if (type == EXEC_FILE)
  {
    /* Load file at physical memory. */
    if (!load_file(frame, spte))
    {
      free_frame (frame);
      return false;
    }
    frame->spte->is_loaded = true;
    frame->spte->type = MEMORY;
  }
  else if (type == MMAP_FILE)
  {
    /* Load file at physical memory. */
    if (!load_file(frame, spte))
    {
      free_frame (frame);
      return false;
    }
    frame->spte->is_loaded = true;
  }
  else if (type == SWAP_DISK)
  {
    /* Swap in physical memory, and load file at physical memory. */
    swap_in(frame->spte->swap_location, frame->kaddr);
    if (!install_page(spte->vaddr, frame->kaddr, spte->writable))
    {
      free_frame (frame);
      return false;
    }
    frame->spte->type = MEMORY;
  }
  return true;
}

bool stack_growth(uint32_t addr)
{
  /* Set vaddr, spte, fte. */
  uint8_t *upage = pg_round_down(addr);
  struct spte *spte = calloc(1, sizeof (struct spte));
  struct fte *frame = alloc_fte ((PAL_USER | PAL_ZERO), spte);

  /* Insert additional stack at physical memory. */
  if (spte == NULL)
  {
    free_frame_perfect (frame);
    return false;
  }
  if (frame != NULL) 
  {
    if (install_page (upage, frame->kaddr, true))
    {
      spte->type = MEMORY;
      spte->vaddr = upage;
      spte->writable = true;
      spte->is_loaded = true;

      if (!insert_spte (&thread_current()->spt, spte))
      {
        free_frame_perfect (frame);
        return false;
      }
    }
    else
    {
      free_frame_perfect (frame);
      return false;
    }
  }
  else
  {
    free_frame_perfect (frame);
    return false;
  }
  return true;
}
