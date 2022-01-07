#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/cache.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

struct block *fs_device;

static void do_format(void);

void
filesys_init(bool format)
{
	fs_device = block_get_role(BLOCK_FILESYS);
	if (fs_device == NULL)
		PANIC("No file system device found, can't initialize file system.");

	inode_init();
	free_map_init();

	buffer_cache_init();

	if (format)
		do_format();

	free_map_open();
}

void
filesys_done(void)
{
	free_map_close();

	buffer_cache_terminate();
}

bool
filesys_create(const char *path, off_t initial_size, bool is_dir)
{
	block_sector_t inode_sector = 0;

	char d[strlen(path)];
	char f[strlen(path)];
	extract_directory_filename_from_path(path, d, f);
	struct dir *dir = dir_open_from_path(d);

	bool success = (dir != NULL
		&& free_map_allocate(1, &inode_sector)
		&& inode_create(inode_sector, initial_size, is_dir)
		&& dir_add(dir, f, inode_sector, is_dir));

	if (!success && inode_sector != 0)
		free_map_release(inode_sector, 1);
	dir_close(dir);

	return success;
}

struct file *
	filesys_open(const char *name)
{
	if (strlen(name) == 0){
		return NULL;
	}

	char d[strlen(name) + 1];
	char f[strlen(name) + 1];
	extract_directory_filename_from_path(name, d, f);
	struct dir *dir = dir_open_from_path(d);
	struct inode *inode = NULL;

	if (dir == NULL){
		return NULL;
	}
	else if (strlen(f) > 0) {
		dir_lookup(dir, f, &inode);
		dir_close(dir);
	}
	else {
		inode = dir_get_inode(dir);
	}

	if (inode == NULL)
		return NULL;
	else if(inode->removed)
		return NULL;

	return file_open(inode);
}

bool
filesys_remove(const char *name)
{
	char d[strlen(name)];
	char f[strlen(name)];
	extract_directory_filename_from_path(name, d, f);
	struct dir *dir = dir_open_from_path(d);

	if(dir == NULL){
		dir_close(dir);
		return false;
	}
	if(!dir_remove(dir,f)){
		dir_close(dir);
		return false;
	}

	dir_close(dir);
	return true;
}

/* Formats the file system. */
static void
do_format(void)
{
	printf("Formatting file system...");
	free_map_create();
	if (!dir_create(ROOT_DIR_SECTOR, 16))
		PANIC("root directory creation failed");
	free_map_close();
	printf("done.\n");
}

bool filesys_chdir(const char *dir_path)
{
	struct dir *d = NULL;

	if ((d = dir_open_from_path(dir_path)) == NULL) return false;

	dir_close(thread_current()->current_dir);

	thread_current()->current_dir = d;

	return true;
}
