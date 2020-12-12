#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/buffer_cache.h"
#include "filesys/directory.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Make inode_disk size to BLOCK_SECTOR_SIZE */
#define DIRECT_BLOCK_ENTRIES 123
#define INDIRECT_BLOCK_ENTRIES (BLOCK_SECTOR_SIZE/sizeof(block_sector_t))

enum sector_type {DIRECT, INDIRECT, DOUBLE_INDIRECT, ERROR};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t is_file;                   /* If file type == FILE this variable become 1.
                                           If file type == DIRECTORY this variable become false.  */
    block_sector_t direct_block_table[DIRECT_BLOCK_ENTRIES];               /* First data sector. */
    block_sector_t indirect_sector;
    block_sector_t double_indirect_sector;
  };

struct block_location
{
  int type;
  int index1;
  int index2;
};

struct indirect_block
{
  block_sector_t indirect_block_table[INDIRECT_BLOCK_ENTRIES];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock lock;                /* Lock for extensible file */
  };

void
locate_byte (off_t position, struct block_location *block_loca)
{
  off_t position_sector = position / BLOCK_SECTOR_SIZE;

  block_loca->type = ERROR;

  if (position_sector < DIRECT_BLOCK_ENTRIES)
  {
    /* Put information. */
    block_loca->type = DIRECT;
    block_loca->index1 = position_sector;
    block_loca->index2 = -1;                  /* Not used. */
  }
  else if (position_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES)
  {
    /* Adjust position sector. */
    position_sector -= DIRECT_BLOCK_ENTRIES;

    /* Put information. */
    block_loca->type = INDIRECT;
    block_loca->index1 = position_sector;
    block_loca->index2 = -1;                  /* Not used. */
  }
  else if (position_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES
                   + INDIRECT_BLOCK_ENTRIES*INDIRECT_BLOCK_ENTRIES)
  {
    /* Adjust position sector. */
    position_sector -= (DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES);

    /* Put information. */
    block_loca->type = DOUBLE_INDIRECT;
    block_loca->index1 = position_sector / INDIRECT_BLOCK_ENTRIES;
    block_loca->index2 = position_sector % INDIRECT_BLOCK_ENTRIES;
  }
}

bool
update_sector (struct inode_disk *inode_disk, block_sector_t new_sector,
                    struct block_location block_loca)
{
  struct indirect_block *new_block;
  struct indirect_block *second_block;
  block_sector_t* sector;

  switch (block_loca.type)
  {
    case DIRECT:
      inode_disk->direct_block_table[block_loca.index1] = new_sector;
      return true;
      break;

    case INDIRECT:

      new_block = calloc (1, BLOCK_SECTOR_SIZE);
      if (new_block == NULL)
        return false;
      
      sector = &inode_disk->indirect_sector;
      /* indirect_sector가 존재하지 않는다면 만들어줘야함. */
      if (*sector == -1)
      {
        if (!free_map_allocate (1, sector))
        {
          free(new_block);
          return false;
        }
        memset(new_block, -1, BLOCK_SECTOR_SIZE);
      }
      /* 존재한다면 읽어옴. */
      else
        buffer_cache_read(*sector, new_block, 0, BLOCK_SECTOR_SIZE, 0);

      /* 이후 sector 정보 저장. */
      new_block->indirect_block_table[block_loca.index1] = new_sector;

      /* 바뀐 정보 file disk에 재입력. */
      buffer_cache_write(*sector, new_block, 0, BLOCK_SECTOR_SIZE, 0);
      free(new_block);
      return true;
      break;

    case DOUBLE_INDIRECT:
      new_block = calloc (1, BLOCK_SECTOR_SIZE);
      if (new_block == NULL)
        return false;

      sector = &inode_disk->double_indirect_sector;
      /* indirect_sector가 존재하지 않는다면 만들어줘야함. */
      if (*sector == -1)
      {
        if (!free_map_allocate (1, sector))
        {
          free(new_block);
          return false;
        }
        memset(new_block, -1, BLOCK_SECTOR_SIZE);
      }
      /* 존재한다면 읽어옴. */
      else
        buffer_cache_read(*sector, new_block, 0, BLOCK_SECTOR_SIZE, 0);
      
      /* 참조할 second indirect block의 sector를 가져온다. */
      block_sector_t *temp = &new_block->indirect_block_table[block_loca.index1];
      // buffer_cache_write(inode_disk->double_indirect_sector, new_block, 0, BLOCK_SECTOR_SIZE, 0);

      /* Allocate second indirect block. */
      second_block = calloc (1, BLOCK_SECTOR_SIZE);
      if (second_block == NULL)
      {
        free(new_block);
        return false;
      }

      /* second indirect_sector가 존재하지 않는다면 만들어줘야함. */
      if (*temp == -1)
      {
        if (!free_map_allocate (1, temp))
        {
          free(new_block);
          return false;
        }
        memset(second_block, -1, BLOCK_SECTOR_SIZE);
      }
      /* 존재한다면 읽어옴. */
      else
        buffer_cache_read(*temp, second_block, 0, BLOCK_SECTOR_SIZE, 0);

      /* second indirect block 접근후 해당 sector 수정. */
      second_block->indirect_block_table[block_loca.index2] = new_sector;

      /* 바뀐 정보 재입력 */
      buffer_cache_write(inode_disk->double_indirect_sector, new_block, 0, BLOCK_SECTOR_SIZE, 0);
      buffer_cache_write(*temp, second_block, 0, BLOCK_SECTOR_SIZE, 0);
      free(second_block);
      free(new_block);
      return true;
      break;
  }
  return false;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  if (pos < inode_disk->length)
  {
    struct indirect_block temp_block;
    struct indirect_block second_block;
    struct block_location block_loca;
    block_sector_t answer;
    locate_byte(pos, &block_loca);
    // printf("byte_to_sector : type %d, index1 %d, index2 %d\n", block_loca.type, block_loca.index1, block_loca.index2);
    switch (block_loca.type)
    {
      case DIRECT:
        return inode_disk->direct_block_table[block_loca.index1];
        break;

      case INDIRECT:
        /* first indirect block 접근후 해당 sector 수정 */
        if (inode_disk->indirect_sector == -1)
          return -1;
        buffer_cache_read(inode_disk->indirect_sector, &temp_block, 0, BLOCK_SECTOR_SIZE, 0);
        answer = temp_block.indirect_block_table[block_loca.index1];
        return answer;
        break;

      case DOUBLE_INDIRECT:
        // printf("byte_to_sector : type %d, index1 %d, index2 %d\n", block_loca.type, block_loca.index1, block_loca.index2);
        /* first indirect block 접근, second indirect block에 접근하기 위한 sector 확보. */ 
        if (inode_disk->double_indirect_sector == -1)
          return -1;
        buffer_cache_read(inode_disk->double_indirect_sector, &temp_block, 0, BLOCK_SECTOR_SIZE, 0);
        block_sector_t temp_sector = temp_block.indirect_block_table[block_loca.index1];

        /* second indirect block 접근후 해당 sector 수정. */
        if (temp_sector == -1)
          return -1;
        buffer_cache_read(temp_sector, &second_block, 0, BLOCK_SECTOR_SIZE, 0);
        answer = second_block.indirect_block_table[block_loca.index2];
        // printf("result %d\n", answer);
        return answer;
        break;
    }
  }
  else
    return -1;
}

bool
update_file_length (struct inode_disk *inode_disk, off_t start_len, off_t end_len)
{
  // printf("update_file_length() : start %d end %d\n", start_len, end_len);
  /* Check valid input length. */
  if (start_len > end_len)
    return false;
  else if (start_len == end_len)
  {
    return true;
  }

  inode_disk->length = end_len;
  // printf("update() : update length %d\n", inode_disk->length);
  off_t temp = start_len;
  static char init_block[BLOCK_SECTOR_SIZE];
  memset(init_block, 0, BLOCK_SECTOR_SIZE);
  off_t chunk_size;

  while (temp < end_len)
  {
    /* Check start point is already in allocated block. */
    off_t sector_ofs = temp % BLOCK_SECTOR_SIZE;
    if (sector_ofs > 0)
      /* Block단위의 시작점을 맞춰 주기 위한 작업. */
      chunk_size = BLOCK_SECTOR_SIZE - sector_ofs;
    else
    {
      chunk_size = BLOCK_SECTOR_SIZE;
      struct block_location block_loca;

      /* temp만큼의 length에 해당하는 block이 이미 존재하는지 확인한다. */
      block_sector_t sector = byte_to_sector(inode_disk, temp);
      // printf("update_file_length() : temp %d, new sector %d\n", temp, sector);
      if (sector == -1 || sector > 16383)
      {
        /* block이 없다는 의미이므로 만들어주고 그 정보를 저장한다. */
        if (!free_map_allocate (1, &sector))
          return false;
        locate_byte(temp, &block_loca);
        if (!update_sector (inode_disk, sector, block_loca))
          return false;
        // block_write (fs_device, sector, init_block);
        buffer_cache_write(sector, init_block, 0, chunk_size, 0);
      }
      // printf("update_file_length() : new sector %d\n", sector);
    }

    /* Update offset. */
    temp += chunk_size;
  }
  return true;
}

void
free_inode_sectors (struct inode_disk *inode_disk)
{
  /* indirect_block을 담을 변수 설정. */
  struct indirect_block temp_block;
  struct indirect_block second_block;

  /* Double_indirect부터 해제. */
  if (inode_disk->double_indirect_sector > -1)
  {
    /* First indirect block 검색, release. */
    buffer_cache_read(inode_disk->double_indirect_sector, &temp_block, 0, BLOCK_SECTOR_SIZE, 0);
    for (int i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
    {
      if (temp_block.indirect_block_table[i] > -1)
      {
        /* Second indirect block 검색, release. */
        off_t second_sector = temp_block.indirect_block_table[i];
        buffer_cache_read(second_sector, &second_block, 0, BLOCK_SECTOR_SIZE, 0);
        for (int j = 0; j < INDIRECT_BLOCK_ENTRIES; j++)
        {
          if (second_block.indirect_block_table[j] > -1)
          {
            free_map_release (second_block.indirect_block_table[j], 1);
            second_block.indirect_block_table[j] = -1;
          }
          else
            break;
        }
        temp_block.indirect_block_table[i] = -1;
      }
      else
        break;
    }
    inode_disk->double_indirect_sector = -1;
  }

  /* Single indirect 해제. */
  if (inode_disk->indirect_sector > -1)
  {
    buffer_cache_read(inode_disk->indirect_sector, &temp_block, 0, BLOCK_SECTOR_SIZE, 0);
    for (int i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
    {
      if (temp_block.indirect_block_table[i] > -1)
        free_map_release (temp_block.indirect_block_table[i], 1);
      else
        break;
    }
  }
  for (int i = 0; i < DIRECT_BLOCK_ENTRIES; i++)
  {
    if (inode_disk->direct_block_table[i] > -1)
      free_map_release (inode_disk->direct_block_table[i], 1);
    else
      break;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_file)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, BLOCK_SECTOR_SIZE);
  if (disk_inode != NULL)
  {
    memset(disk_inode, -1, BLOCK_SECTOR_SIZE);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->is_file = is_file;
    if (!update_file_length (disk_inode, 0, disk_inode->length))
    {
      free(disk_inode);
      return success;
    }
    // printf("inode_create : inode_disk length %d\n", disk_inode->length);
    buffer_cache_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
    free(disk_inode);
    success = true;
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  // printf("inode_close() : start\n");
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      lock_acquire(&inode->lock);
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          // printf("inode_close() : removed\n");
          struct inode_disk *inode_disk = calloc(1, BLOCK_SECTOR_SIZE);
          if (inode_disk != NULL)
          {
            lock_release(&inode->lock);
            buffer_cache_read(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);
            lock_acquire(&inode->lock);
            free_inode_sectors (inode_disk);
            free (inode_disk);
          }
          free_map_release (inode->sector, 1);
        }
      lock_release(&inode->lock);
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  /* Change inode's information. */
  lock_acquire(&inode->lock);

  /* Get inode disk. */
  struct inode_disk *inode_disk = calloc(1, BLOCK_SECTOR_SIZE);
  if (inode_disk == NULL)
    return 0;
  buffer_cache_read(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);

      // lock_release(&inode->lock);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_disk->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
      {
        // lock_acquire(&inode->lock);
        break;
      }

      buffer_cache_read (sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
      // printf("data is %s\n", buffer);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
      // lock_acquire(&inode->lock);
    }
  free(inode_disk);
  lock_release(&inode->lock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  // printf("inode_write_at() : size %d, offset %d.\n", size, offset);
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  /* Change inode's information. */
  lock_acquire(&inode->lock);

  /* Get inode disk. */
  struct inode_disk *inode_disk = calloc(1, BLOCK_SECTOR_SIZE);
  if (inode_disk == NULL)
    return 0;
  buffer_cache_read(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);

  /* Extense file. */
  update_file_length (inode_disk, inode_disk->length, offset+size);
  buffer_cache_write(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);
      // printf("inode_write_at() : sector_idx is %d.\n", sector_idx);
      // lock_release(&inode->lock);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_disk->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
      {
        // lock_acquire(&inode->lock);
        break;
      }

      buffer_cache_write (sector_idx, (void *)buffer, bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
      // lock_acquire(&inode->lock);
    }
  free(inode_disk);
  lock_release(&inode->lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *inode_disk = calloc(1, BLOCK_SECTOR_SIZE);
  if (inode_disk == NULL)
    return -1;
  buffer_cache_read(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);

  off_t answer = inode_disk->length;
  free(inode_disk);
  return answer;
}

bool
inode_is_file (const struct inode *inode)
{
  if (inode->removed)
    return true;
  
  struct inode_disk *inode_disk = calloc(1, BLOCK_SECTOR_SIZE);
  if (inode_disk == NULL)
    return -1;
  buffer_cache_read(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);
  
  uint32_t is_file = inode_disk->is_file;
  free(inode_disk);
  return is_file;
}

struct dir *
parse_path (char * path, char *file_name)
{
  // printf("----------------------path is %s\n", path);
  struct dir *dir = NULL;
  if (path == NULL || strlen(path) == 0 || file_name == NULL)
    return NULL;
  
  struct inode *inode = NULL;
  /* Open dir by first character of path. */
  if (path[0] == '/')    /* "/~~~" */
    dir = dir_open_root();
  else                  /* "~~~~/ or \0" */
    dir = dir_reopen(thread_current()->dir);

  if (inode_is_file(dir_get_inode(dir)))
    return NULL;
  
  /* Parse file path. */
  char *token, *next_token, *next_ptr;

  // printf("========path is %s\n", path);
  token = strtok_r(path, "/", &next_ptr);
  // printf("========token is %s\n", token);
  next_token = strtok_r(NULL, "/", &next_ptr);
  // printf("========token2 is %s\n", next_token);
  

  if (token == ".")
  {
    strlcpy(file_name, token, NAME_MAX + 1);
    return dir;
  }

  while (token != NULL && next_token != NULL) 
  {
    // printf("========token is %s, next is %s\n", token, next_token);
    if (!dir_lookup (dir, token, &inode))
    {
      // printf("lockup fail.\n");
      strlcpy(file_name, token, NAME_MAX + 1);
      return dir;
    }
    if (inode_is_file (inode))
    {
      // printf("inode is file.\n");
      // strlcpy(file_name, token, NAME_MAX + 1);
      return dir;
    }

    dir_close(dir);
    dir = dir_open(inode);
    /* Split whole sentence by '/'. */
    // strlcpy(file_name, token, NAME_MAX + 1);
    token = next_token;
    next_token = strtok_r(NULL, "/", &next_ptr);
  }

  /* If try removing root dir */
  if (token == NULL)
  {
    // printf("path is / \n");
    strlcpy(file_name, ".", NAME_MAX + 1);
  }
  else
    strlcpy(file_name, token, NAME_MAX + 1);
  return dir;
}
