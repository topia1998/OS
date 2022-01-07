#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);
/* Directory and Path manipulation utilities. */
void split_path_filename(const char *path, char *directory, char *filename);


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

  buffer_cache_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();

  buffer_cache_terminate ();
}

/* Creates a file or directory (set by `is_dir`) of
   full path `path` with the given `initial_size`.
   The path to file consists of two parts: path directory and filename.

   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size, bool is_dir)
{
  block_sector_t inode_sector = 0;

  // split path and name
  char directory[ strlen(path) ];
  char file_name[ strlen(path) ];
  split_path_filename(path, directory, file_name);
  struct dir *dir = dir_open_path (directory);

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, file_name, inode_sector, is_dir));

  just_in_case();
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

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
  int l = strlen(name);
  if (l == 0) return NULL;

  char directory[ l + 1 ];
  char file_name[ l + 1 ];
  split_path_filename(name, directory, file_name);
  struct dir *dir = dir_open_path (directory);
  struct inode *inode = NULL;

  just_in_case();
  // removed directory handling
  if (dir != NULL) {
    if (strlen(file_name) > 0) {
      dir_lookup (dir, file_name, &inode);
    }
    else { // empty filename : just return the directory
      inode = dir_get_inode (dir);
    }
    // removed file handling
    if (inode == NULL || inode_is_removed (inode))
      return NULL;
    dir_close (dir);
  }
  else
    return NULL;

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  char directory[ strlen(name) ];
  char file_name[ strlen(name) ];
  split_path_filename(name, directory, file_name);
  struct dir *dir = dir_open_path (directory);

  just_in_case();
  bool success = (dir != NULL && dir_remove (dir, file_name));
  dir_close (dir);

  return success;
}

/* Change CWD for the current thread. */
bool
filesys_chdir (const char *name)
{
  struct dir *dir = dir_open_path (name);

  if(dir == NULL) {
    return false;
  }
  just_in_case();
  // switch CWD
  dir_close (thread_current()->cwd);
  thread_current()->cwd = dir;
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

void
split_path_filename(const char *path, char *directory, char *filename)
{
  int l = strlen(path);
  char *s = (char*) malloc( sizeof(char) * (l + 1) );
  memcpy (s, path, sizeof(char) * (l + 1));

  // absolute path handling
  char *dir = directory;
  if(l > 0 && path[0] == '/') {
    if(dir) *dir++ = '/';
  }

  // tokenize
  char *token, *p, *last_token = "";
  for (token = strtok_r(s, "/", &p); token != NULL;
       token = strtok_r(NULL, "/", &p))
  {
    // append last_token into directory
    int tl = strlen (last_token);
    if (dir && tl > 0) {
      memcpy (dir, last_token, sizeof(char) * tl);
      dir[tl] = '/';
      dir += tl + 1;
    }

    last_token = token;
  }

  if(dir) *dir = '\0';
  memcpy (filename, last_token, sizeof(char) * (strlen(last_token) + 1));
  free (s);

}