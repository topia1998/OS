#include "threads/thread.h"
#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* A directory. */
struct dir
{
	struct inode *inode;                /* Backing store. */
	off_t pos;                          /* Current position. */
};

/* A single directory entry. */
struct dir_entry
{
	block_sector_t inode_sector;        /* Sector number of header. */
	char name[NAME_MAX + 1];            /* Null terminated file name. */
	bool in_use;                        /* In use or free? */
};

bool
dir_create(block_sector_t sector, size_t entry_cnt)
{
	if(!inode_create(sector, entry_cnt * sizeof(struct dir_entry),true))
		return false;

	struct dir *dir = dir_open(inode_open(sector));
	struct dir_entry e;
	e.inode_sector = sector;
	if (inode_write_at(dir->inode, &e, sizeof e, 0) != sizeof e)
		return false;
	dir_close(dir);

	return true;
}

struct dir *
	dir_open(struct inode *inode)
{
	struct dir *dir = calloc(1, sizeof *dir);
	if (inode != NULL && dir != NULL)
	{
		dir->inode = inode;
		dir->pos = sizeof(struct dir_entry); // 0-pos is for parent directory
		return dir;
	}
	else
	{
		inode_close(inode);
		free(dir);
		return NULL;
	}
}

struct dir *
	dir_open_root(void)
{
	return dir_open(inode_open(ROOT_DIR_SECTOR));
}


/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
	dir_reopen(struct dir *dir)
{
	return dir_open(inode_reopen(dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close(struct dir *dir)
{
	if (dir != NULL)
	{
		inode_close(dir->inode);
		free(dir);
	}
}

/* Returns the inode encapsulated by DIR. */
struct inode *
	dir_get_inode(struct dir *dir)
{
	ASSERT(dir != NULL);
	return dir->inode;
}

static bool
lookup(const struct dir *dir, const char *name,
	struct dir_entry *ep, off_t *ofsp)
{
	struct dir_entry e;
	size_t ofs;

	ASSERT(dir != NULL);
	ASSERT(name != NULL);

	for (ofs = sizeof e; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e;
		ofs += sizeof e)
		if (e.in_use && !strcmp(name, e.name))
		{
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
	return false;
}

bool
dir_lookup(const struct dir *dir, const char *name,
	struct inode **inode)
{
	struct dir_entry e;

	ASSERT(dir != NULL);
	ASSERT(name != NULL);

	if (strcmp(name, ".") == 0) {
		*inode = inode_reopen(dir->inode);
	}
	else if (strcmp(name, "..") == 0) {
		inode_read_at(dir->inode, &e, sizeof e, 0);
		*inode = inode_open(e.inode_sector);
	}
	else if (lookup(dir, name, &e, NULL)) {
		*inode = inode_open(e.inode_sector);
	}
	else
		*inode = NULL;

	return *inode != NULL;
}

bool
dir_add(struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir)
{
	struct dir_entry e;
	off_t ofs;

	ASSERT(dir != NULL);
	ASSERT(name != NULL);

	/* Check NAME for validity. */
	if (*name == '\0' || strlen(name) > NAME_MAX)
		return false;

	/* Check that NAME is not in use. */
	if (lookup(dir, name, NULL, NULL))
		return false;

	if (is_dir == true)
	{
		struct dir *new_directory = dir_open(inode_open(inode_sector));
		if (new_directory == NULL)
			return false;
		e.inode_sector = inode_get_inumber(dir_get_inode(dir));
		if (inode_write_at(new_directory->inode, &e, sizeof e, 0) != sizeof e) {
			dir_close(new_directory);
			return false;
		}
		dir_close(new_directory);
	}

	for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e;
		ofs += sizeof e)
		if (!e.in_use)
			break;

	/* Write slot. */
	e.in_use = true;
	strlcpy(e.name, name, sizeof e.name);
	e.inode_sector = inode_sector;
	return inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e;
}

bool
dir_remove(struct dir *dir, const char *name)
{
	struct dir_entry e;
	struct inode *inode = NULL;
	bool success = false;
	off_t ofs;

	ASSERT(dir != NULL);
	ASSERT(name != NULL);

	if (!lookup(dir, name, &e, &ofs))
		goto done;

	inode = inode_open(e.inode_sector);
	if (inode == NULL)
		goto done;

	if (inode->data.is_dir) {
		struct dir *d = dir_open(inode);

		struct dir_entry e;
		off_t ofs;

		for(ofs = sizeof e; inode_read_at(d->inode, &e, sizeof e, ofs) == sizeof e;
				ofs += sizeof e){
			if(e.in_use == true){
				dir_close(d);
				goto done;
			}
		}
		dir_close(d);
	}

	e.in_use = false;
	if (inode_write_at(dir->inode, &e, sizeof e, ofs) != sizeof e)
		goto done;

	inode_remove(inode);
	success = true;

done:
	inode_close(inode);
	return success;
}

bool
dir_readdir(struct dir *dir, char name[NAME_MAX + 1])
{
	struct dir_entry e;

	while (inode_read_at(dir->inode, &e, sizeof e, dir->pos) == sizeof e)
	{
		dir->pos += sizeof e;
		if (e.in_use)
		{
			strlcpy(name, e.name, NAME_MAX + 1);
			return true;
		}
	}
	return false;
}

void extract_directory_filename_from_path(const char *path, char *directory, char *filename)
{
	char *str = (char*)malloc(sizeof(char) * (strlen(path) + 1));

	char *dir = directory;
	if (strlen(path) > 0 && path[0] == '/') {
		if (dir){
			*dir = '/';
			dir++;
		}
	}

	char *chunk, *next, *chunk2 = "\0";

	memcpy(str, path, sizeof(char) * (strlen(path) + 1));

	for (chunk = strtok_r(str, "/", &next); chunk != NULL; chunk = strtok_r(NULL, "/", &next))
	{
		if (dir != 0 && strlen(chunk2) != 0) {
			memcpy(dir, chunk2, sizeof(char) * strlen(chunk2));
			dir[strlen(chunk2)] = '/';
			dir += strlen(chunk2);
			dir++;
		}

		chunk2 = chunk;
	}
	if (dir != 0){
		*dir = '\0';
	}
	memcpy(filename, chunk2, sizeof(char) * (strlen(chunk2) + 1));
	free(str);
}

struct dir * dir_open_from_path(const char *path)
{
	char str[strlen(path) + 1];

	struct dir *cd;
	if (path[0] == '/')
		cd = dir_open_root();
	else if (thread_current()->current_dir != NULL)
		cd = dir_reopen(thread_current()->current_dir);
	else
		cd = dir_open_root();
	
	char *chunk, *next_chunk;
	struct inode *inode = NULL;
	struct dir *next = NULL;

	strlcpy(str, path, strlen(path) + 1);
	for (chunk = strtok_r(str, "/", &next_chunk); chunk != NULL; chunk = strtok_r(NULL, "/", &next_chunk))
	{
		inode = NULL;

		if (dir_lookup(cd, chunk, &inode) == false) {
			dir_close(cd);
			return NULL;
		}
		else if((next = dir_open(inode)) == NULL){
			dir_close(cd);
			return NULL;
		}
		else{
			dir_close(cd);
			cd = next;
		}
	}

	if (dir_get_inode(cd)->removed == true) {
		dir_close(cd);
		return NULL;
	}

	return cd;
}

