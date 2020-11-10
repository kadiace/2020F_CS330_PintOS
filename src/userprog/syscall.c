#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
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
  int return_val = filesys_create(file, initial_size);
  return return_val;
}

bool
remove (const char *file)
{
  if(file == NULL) {exit(-1);}
  int return_val = filesys_remove(file);
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
