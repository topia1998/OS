#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include <list.h>

/* In-memory inode. */
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
#define DIRECT 123
struct inode_disk
{
	block_sector_t direct_blocks[DIRECT];
	block_sector_t indirect_block;
	block_sector_t doubly_indirect_block;
	bool is_dir;
	off_t length;                       /* File size in bytes. */
	unsigned magic;                     /* Magic number. */
};
struct inode
{
	struct list_elem elem;              /* Element in inode list. */
	block_sector_t sector;              /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
};

struct bitmap;

void inode_init(void);
bool inode_create(block_sector_t, off_t, bool);
struct inode *inode_open(block_sector_t);
struct inode *inode_reopen(struct inode *);
block_sector_t inode_get_inumber(const struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(const struct inode *);

block_sector_t get_sector_number(const struct inode_disk *, off_t);
bool allocate_block(block_sector_t*);
bool alloc_inode(struct inode_disk *, off_t);
bool alloc_inode_doubly_indirect(block_sector_t*, size_t);
bool alloc_inode_indirect(block_sector_t*, size_t);
void dealloc_inode_doubly_indirect(block_sector_t, size_t);
void dealloc_inode_indirect(block_sector_t, size_t);
bool dealloc_inode(struct inode *);
bool inode_dir(const struct inode *);
#endif /* filesys/inode.h */
