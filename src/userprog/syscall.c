#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/off_t.h"
#include "vm/page.h"

/* Take struct file to use deny_write at open(), write(). */
struct file 
{
  struct inode *inode;        /* File's inode. */
  off_t pos;                  /* Current position. */
  bool deny_write;            /* Has file_deny_write() been called? */
};

/* validate esp address is available to the user */
void check_esp (void *esp_value)
{
  if (!is_user_vaddr(esp_value) || esp_value == NULL)
    exit(-1);
}

/* For synchronization file when open, read, write */
struct lock file_lock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // printf("system call : %d\n", *(uint32_t *)(f->esp));
  switch (*(uint32_t *)(f->esp))
  {
    case SYS_HALT :
      halt ();
      break;

    case SYS_EXIT :
      check_valid_string (f->esp + 4, f->esp);
      exit (*(uint32_t *)(f->esp + 4));
      break;

    case SYS_EXEC :
      check_valid_string (f->esp + 4, f->esp);
      f->eax = exec ((const char *)*(uint32_t *)(f->esp + 4));
      break;

    case SYS_WAIT :
      check_valid_string (f->esp + 4, f->esp);
      f->eax = wait ((pid_t)*(uint32_t *)(f->esp + 4));
      break;

    case SYS_CREATE :
      check_valid_string (f->esp + 4, f->esp);
      check_valid_string (f->esp + 8, f->esp);
      f->eax = create ((const char *)*(uint32_t *)(f->esp + 4), (unsigned)*(uint32_t *)(f->esp + 8));
      break;

    case SYS_REMOVE :
      check_valid_string (f->esp + 4, f->esp);
      f->eax = remove ((const char *)*(uint32_t *)(f->esp + 4));
      break;

    case SYS_OPEN :
      check_valid_string (f->esp + 4, f->esp);
      f->eax = open ((const char *)*(uint32_t *)(f->esp + 4));
      break;

    case SYS_FILESIZE :
      check_valid_string (f->esp + 4, f->esp);
      f->eax = filesize ((int) *(uint32_t *)(f->esp + 4));
      break;

    case SYS_READ :
      check_valid_string (f->esp + 4, f->esp);
      check_valid_buffer ((void *) *(uint32_t *)(f->esp + 8), (unsigned) *((uint32_t *)(f->esp + 12)), f->esp, true);
      f->eax = read ((int) *(uint32_t *)(f->esp + 4), (void *) *(uint32_t *)(f->esp + 8),
        (unsigned) *((uint32_t *)(f->esp + 12)));
      break;

    case SYS_WRITE :
      check_valid_string (f->esp + 4, f->esp);
      check_valid_buffer ((void *) *(uint32_t *)(f->esp + 8), (unsigned) *((uint32_t *)(f->esp + 12)), f->esp, false);
      f->eax = write ((int) *(uint32_t *)(f->esp + 4), (void *) *(uint32_t *)(f->esp + 8), 
        (unsigned) *((uint32_t *)(f->esp + 12)));
      break;

    case SYS_SEEK :
      check_valid_string (f->esp + 4, f->esp);
      check_valid_string (f->esp + 8, f->esp);
      seek ((int) *(uint32_t *)(f->esp + 4), (unsigned) *((uint32_t *)(f->esp + 8)));
      break;

    case SYS_TELL :
      check_valid_string (f->esp + 4, f->esp);
      f->eax = tell ((int) *(uint32_t *)(f->esp + 4));
      break;

    case SYS_CLOSE :
      check_valid_string (f->esp + 4, f->esp);
      close ((int) *(uint32_t *)(f->esp + 4));
      break;

    case SYS_MMAP :
      check_valid_string (f->esp + 4, f->esp);
      check_valid_string (f->esp + 8, f->esp);
      f->eax = mmap ((int) *(uint32_t *)(f->esp + 4), (unsigned) *((uint32_t *)(f->esp + 8)));
      break;

    case SYS_MUNMAP :
      check_valid_string (f->esp + 4, f->esp);
      munmap ((mapid_t) *(uint32_t *)(f->esp + 4));
      break;
  }
}

void
halt(void)
{
  shutdown_power_off();
}

void
exit (int status)
{
  /* Get current thread, update exit_status, and close whole files. */
  struct thread *cur = thread_current();
  cur->exit_status = status;
  
  /* Print exit message. */
  printf("%s: exit(%d)\n", cur->name, status);

  /* Exit. */
  thread_exit();
}

pid_t
exec (const char *cmd_line)
{
  return process_execute(cmd_line);
}

int
wait (pid_t pid) {
  return process_wait(pid);
}

bool
create (const char *file, unsigned initial_size)
{
  if(file == NULL) {exit(-1);}
  lock_acquire(&file_lock);
  int return_val = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return return_val;
}

bool
remove (const char *file)
{
  if(file == NULL) {exit(-1);}
  lock_acquire(&file_lock);
  int return_val = filesys_remove(file);
  lock_release(&file_lock);
  return return_val;
}

int
open (const char *file)
{
  if (file == NULL)
  {
    return -1;
  }
  lock_acquire(&file_lock);
  /* Change file type to struct file *. */
  struct file *result = filesys_open(file);
  if (result == NULL)
  {
    lock_release(&file_lock);
    return -1;
  }
  struct file **table = thread_current()->fd_table;

  /* Check user's empty file descriptor. */
  for (int i = 2;i < 128; i++)
  {
    if (table[i] == NULL)
    {
      /* if current thread uses some files, then other can't access these files. */
      if (strcmp(thread_current()->name, file) == 0)
      {
        file_deny_write(result);
      }
      table[i] = result;
      lock_release(&file_lock);
      return i;
    }
  }
  lock_release(&file_lock);
  return -1;
}

int
filesize (int fd) 
{
  struct file *file = thread_current()->fd_table[fd];
  return file_length(file);
}

int
read (int fd, void *buffer, unsigned size)
{
  if (!is_user_vaddr(buffer))
  {
    exit(-1);
  }
  lock_acquire(&file_lock);
  struct file *file = thread_current()->fd_table[fd];
  /* If file descriptor is stdout or NULL, return -1. */
  if (fd == 1 || file == NULL)
  {
    lock_release(&file_lock);
    return -1;
  }
  /* If file descriptor is stdin, return read bytes. */
  else if (fd == 0)
  {
    uint8_t *buffer_ = (uint8_t *) buffer;
    int i;
    for(i = 0; i < size; i++)
    {
      buffer_[i] = input_getc();
    }
    lock_release(&file_lock);
    return i;
  }
  /* If file descriptor is user's, return read bytes. */
  int result = file_read(file, buffer, size);
  lock_release(&file_lock);
  return result;
}

int write (int fd, const void *buffer, unsigned size)
{
  lock_acquire(&file_lock);
  struct file *file = thread_current()->fd_table[fd];
  /* If file descriptor is stdout, return written bytes. */
  if (fd == 1)
  {
    putbuf(buffer, size);
    lock_release(&file_lock);
    return size;
  }
  /* If file descriptor is stdin or NULL, return -1. */
  else if (fd == 0 || file == NULL)
  {
    lock_release(&file_lock);
    return 0;
  }
  /* If file descriptor is user's, return written bytes. */
  int result = file_write(file, buffer, size);
  lock_release(&file_lock);
  return result;
}

void
seek (int fd, unsigned position)
{
  struct file *file = thread_current()->fd_table[fd];
  if (file == NULL)
    exit(-1);
  file_seek(file, position);
}

unsigned 
tell (int fd)
{
  struct file *file = thread_current()->fd_table[fd];
  if (file == NULL)
    exit(-1);
  return file_tell(file);
}

void
close (int fd)
{
  struct file *file = thread_current()->fd_table[fd];
  if (file == NULL)
    exit(-1);
  file_close(file);
  thread_current()->fd_table[fd] = NULL;
}

mapid_t
mmap (int fd, void *addr)
{
  /* Fail if addr is not page-aligned, or if addr is 0 */
  if (addr == NULL || (int)addr % PGSIZE != 0 || fd > 127)
    return -1;

  struct map_file *mf = calloc(1, sizeof(struct map_file));
  if (mf == NULL)
    return -1;
  struct thread *cur = thread_current();
  struct file *file = file_reopen(cur->fd_table[fd]);
  /* Fail if the file open as fd has a length of zero bytes */
  if (file == NULL)
  {
    free(mf);
    return -1;
  }

  list_init(&mf->spte_list);
  mf->file = file;

  int file_len = file_length(file);
  int page_read_bytes;
  int page_zero_bytes;
  int ofs = 0;
  uint32_t vaddr = pg_round_down(addr);

  while (file_len > 0)
  {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    page_read_bytes = file_len < PGSIZE ? file_len : PGSIZE;
    page_zero_bytes = PGSIZE - page_read_bytes;

    /* Make spte. */
    struct spte *spte = (struct spte *)calloc (1, sizeof (struct spte));
    if (spte == NULL)
    {
      struct list_elem *base = list_begin(&mf->spte_list);
      while (base != list_tail(&mf->spte_list))
      {
        spte = list_entry(base, struct spte, map_elem);
        delete_spte(&thread_current()->spt, spte);
      }
      free(mf);
      return -1;
    }
    spte->type = MMAP_FILE;
    spte->vaddr = vaddr;
    spte->writable = true;
    spte->is_loaded = false;
    spte->file = file;
    spte->offset = ofs;
    spte->read_bytes = page_read_bytes;
    spte->zero_bytes = page_zero_bytes;

    /* Insert at spt. */
    if (!insert_spte (&thread_current()->spt, spte))
    {
      free(spte);
      struct list_elem *base = list_begin(&mf->spte_list);
      while (base != list_tail(&mf->spte_list))
      {
        spte = list_entry(base, struct spte, map_elem);
        delete_spte(&thread_current()->spt, spte);
      }
      free(mf);
      return -1;
    }
    list_push_back(&mf->spte_list, &spte->map_elem);

    file_len -= page_read_bytes;
    ofs += page_read_bytes;
    vaddr += PGSIZE;
  }

  list_push_back(&cur->mmap_list, &mf->elem);
  cur->map_id +=1;
  mf->map_id = cur->map_id;
  return mf->map_id;
}


void
munmap (mapid_t md)
{
  struct thread *cur = thread_current();
  struct list_elem *base = list_begin(&cur->mmap_list);
  struct list_elem *next;
  struct map_file *mf;
  /* Free mmap which has id md. */
  while (base != list_tail(&cur->mmap_list))
  {
    mf = list_entry(base, struct map_file, elem);
    next = base->next;
    if (mf->map_id == md || md == CLOSE_ALL)
    {
      delete_mmap(mf, cur);
      lock_acquire(&file_lock);
      file_close(mf->file);
      lock_release(&file_lock);
      list_remove(base);
      free(mf);
      if(md != CLOSE_ALL)
        break;
    }
    base = next;
  }
}

extern struct lock frame_lock;

void delete_mmap(struct map_file *mf, struct thread *cur)
{
  struct list_elem *base = list_begin(&mf->spte_list);
  struct list_elem *iter_frame;
  struct list_elem *temp;
  struct fte *frame;
  struct spte *sp;
  while (base != list_tail(&mf->spte_list))
  {
    temp = list_remove(base);
    sp = list_entry(base, struct spte, map_elem);
    /* If page fault occurred once, delete frame and spte. */
    if (sp->is_loaded)
    {
      /* If something is written on mmap, then write back to file. */
      if (pagedir_is_dirty(&cur->pagedir, sp->vaddr))
      {
        lock_acquire(&file_lock);
        file_write_at(sp->file, sp->vaddr, sp->read_bytes, sp->offset);
        lock_release(&file_lock);
      }
      /* Find spte that connected with frame and free all of them. */
      lock_acquire(&frame_lock);
      iter_frame = list_begin(get_frame_table());
      while (iter_frame != list_tail(get_frame_table()))
      {
        frame = list_entry(iter_frame, struct fte, elem);
        if (frame->spte == sp)
        {
          lock_release(&frame_lock);
          free_frame_perfect(frame);
          lock_acquire(&frame_lock);
          break;
        }
        iter_frame = iter_frame->next;
      }
      lock_release(&frame_lock);
    }
    /* If page fault not occurred, there is no frame. only delete spte.  */
    else
      delete_spte(&cur->spt, sp);
    base = temp;
  }
}
