#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  /* Initialize buffer cache. */
  buffer_cache_init();

  if (format) 
    do_format ();

  free_map_open ();

  /* Set working directory of current thread to root. */
  thread_current()->dir = dir_open_root ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  buffer_cache_term();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  // printf("name %s, size %d\n", name, initial_size);
  if (strlen(name) > NAME_MAX)
    return false;

  block_sector_t inode_sector = 0;

  /* Parse path. */
  char *file = calloc(1, NAME_MAX + 1);
  if (file == NULL)
    return false;
  char *name_copy = calloc(1, NAME_MAX * 2);
  if (name_copy == NULL)
  {
    free(file);
    return false;
  }
  strlcpy(name_copy, name, NAME_MAX * 2);
  // printf("name copy %s", name_copy);
  struct dir *dir = parse_path (name_copy, file);

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, TYPE_FILE)
                  && dir_add (dir, file, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

bool
filesys_mkdir (const char *name) 
{
  block_sector_t inode_sector = 0;

  /* Parse path. */
  if (strlen(name) > NAME_MAX)
    return false;
  char *file = calloc(1, NAME_MAX + 1);
  if (file == NULL)
    return false;
  char *name_copy = calloc(1, NAME_MAX * 2);
  if (name_copy == NULL)
  {
    free(file);
    return false;
  }
  strlcpy(name_copy, name, NAME_MAX * 2);
  struct dir *dir = parse_path (name_copy, file);
  // printf("after parse --> file %s -----\n", file);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, NAME_MAX + 1)
                  && dir_add (dir, file, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  if (success)
  {
    struct dir *subdir = dir_open(inode_open(inode_sector));
    
    success = (dir_add (subdir, ".", inode_sector) &&
              dir_add (subdir, "..", inode_get_inumber(dir_get_inode(dir))));
    dir_close (subdir);
  }
  dir_close (dir);

  free(file);
  free(name_copy);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  // printf("filesys_open(): start\n");
  struct inode *inode = NULL;
  
  /* Parse path. */
  char *file = calloc(1, NAME_MAX + 1);
  if (file == NULL)
    return false;
  char *name_copy = calloc(1, NAME_MAX * 2);
  if (name_copy == NULL)
  {
    free(file);
    return false;
  }
  strlcpy(name_copy, name, NAME_MAX * 2);
  struct dir *dir = parse_path (name_copy, file);

  // if (!strcmp(file, "."))
  // {
  //   if (dir != NULL)
  //     dir_lookup (dir, file, &inode);
  //   dir_close (dir);
  // }
  if (dir != NULL)
  {
    // printf("dir is not null\n");
    dir_lookup (dir, file, &inode);
  }
  dir_close (dir);
  free(file);
  free(name_copy);
  // if (dir != NULL)
  //   dir_lookup (dir, name, &inode);
  // dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  /* Parse path. */
  char *file = calloc(1, NAME_MAX + 1);
  if (file == NULL)
    return false;
  char *name_copy = calloc(1, NAME_MAX + 1);
  if (name_copy == NULL)
  {
    free(file);
    return false;
  }
  strlcpy(name_copy, name, NAME_MAX + 1);
  struct dir *dir = parse_path (name_copy, file);

  /* Find subdir. If subdir exist, cancel remove. */
  struct inode *inode = NULL;
  struct dir *cur_dir;
  char *temp = calloc(1, NAME_MAX + 1);
  if (temp == NULL)
  {
    free(file);
    free(name_copy);
    return false;
  }

  bool success = false;
  if (dir != NULL)
  {
    dir_lookup (dir, file, &inode);
    if (!inode_is_file(inode))
    {
      cur_dir = dir_open(inode);
      if (!dir_readdir(cur_dir, temp))
        success = dir_remove (dir, file);
      dir_close(cur_dir);
    }
    else
      success = dir_remove (dir, file);
  }
  dir_close (dir);
  free(file);
  free(name_copy);
  free(temp);
  // struct dir *dir = dir_open_root ();
  // bool success = dir != NULL && dir_remove (dir, name);
  // dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  
  struct dir *dir = dir_open_root();
  dir_add (dir, ".", ROOT_DIR_SECTOR);
  dir_add (dir, "..", ROOT_DIR_SECTOR); 
  free_map_close ();
  printf ("done.\n");
}
