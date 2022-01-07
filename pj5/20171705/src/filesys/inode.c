#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT 123
#define INDIRECT 128


struct indirect_block {
	block_sector_t pointers[INDIRECT];
};

static block_sector_t
byte_to_sector(const struct inode *inode, off_t pos)
{
	ASSERT(inode != NULL);
	if(pos < 0)
		return -1;
	else if ( pos < inode->data.length)
		return get_sector_number(&inode->data, pos/BLOCK_SECTOR_SIZE);
	
	else
		return -1;
}

static struct list open_inodes;

void
inode_init(void)
{
	list_init(&open_inodes);
}

bool
inode_create(block_sector_t sector, off_t length, bool is_dir)
{
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT(length >= 0);

	ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

	disk_inode = calloc(1, sizeof *disk_inode);
	if (disk_inode != NULL)
	{
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		disk_inode->is_dir = is_dir;
		if (alloc_inode(disk_inode,disk_inode->length))
		{
			buffer_cache_write(sector, disk_inode);
			success = true;
		}
		free(disk_inode);
	}
	return success;
}

struct inode *
	inode_open(block_sector_t sector)
{
	struct list_elem *e;
	struct inode *inode;

	for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
		e = list_next(e))
	{
		inode = list_entry(e, struct inode, elem);
		if (inode->sector == sector)
		{
			inode_reopen(inode);
			return inode;
		}
	}

	inode = malloc(sizeof *inode);
	if (inode == NULL)
		return NULL;

	list_push_front(&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;

	buffer_cache_read(inode->sector, &inode->data);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
	inode_reopen(struct inode *inode)
{
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber(const struct inode *inode)
{
	return inode->sector;
}

void
inode_close(struct inode *inode)
{
	if (inode == NULL)
		return;

	if (--inode->open_cnt == 0)
	{
		list_remove(&inode->elem);

		if (inode->removed)
		{
			free_map_release(inode->sector, 1);
			dealloc_inode(inode);
		}

		free(inode);
	}
}

void
inode_remove(struct inode *inode)
{
	ASSERT(inode != NULL);
	inode->removed = true;
}

off_t
inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset)
{
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0)
	{
		/* Disk sector to read, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
		{
			/* Read full sector directly into caller's buffer. */
			buffer_cache_read(sector_idx, buffer + bytes_read);
		}
		else
		{
			/* Read sector into bounce buffer, then partially copy
			   into caller's buffer. */
			if (bounce == NULL)
			{
				bounce = malloc(BLOCK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			buffer_cache_read(sector_idx, bounce);
			memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free(bounce);

	return bytes_read;
}

off_t
inode_write_at(struct inode *inode, const void *buffer_, off_t size,
	off_t offset)
{
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	if (byte_to_sector(inode, offset + size - 1) == -1) {
		if(!alloc_inode(&inode->data, offset + size)){
			return 0;
		}
		inode->data.length = offset + size;
		buffer_cache_write(inode->sector, &inode->data);
	}

	while (size > 0)
	{
		/* Sector to write, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
		{
			/* Write full sector directly to disk. */
			buffer_cache_write(sector_idx, buffer + bytes_written);
		}
		else
		{
			/* We need a bounce buffer. */
			if (bounce == NULL)
			{
				bounce = malloc(BLOCK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left)
				buffer_cache_read(sector_idx, bounce);
			else
				memset(bounce, 0, BLOCK_SECTOR_SIZE);
			memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
			buffer_cache_write(sector_idx, bounce);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free(bounce);

	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write(struct inode *inode)
{
	inode->deny_write_cnt++;
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write(struct inode *inode)
{
	ASSERT(inode->deny_write_cnt > 0);
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length(const struct inode *inode)
{
	return inode->data.length;
}

static char empty_page[BLOCK_SECTOR_SIZE];

bool allocate_block(block_sector_t* block){

	if(*block == 0){
		if(!free_map_allocate(1,block))
			return false;
		buffer_cache_write(*block,empty_page);
	}
	return true;
}

bool
alloc_inode_doubly_indirect(block_sector_t* block, size_t size)
{
	struct indirect_block indirect_block;
	if (*block == 0) {
		free_map_allocate(1, block);
		buffer_cache_write(*block, empty_page);
	}
	buffer_cache_read(*block, &indirect_block);

	size_t l = DIV_ROUND_UP(size, INDIRECT);

	for (size_t i = 0; i < l; i++) {
		size_t alloc_length = size < INDIRECT ? size : INDIRECT;
		if (!alloc_inode_indirect(&indirect_block.pointers[i], alloc_length))
			return false;
		size -= alloc_length;
	}

	buffer_cache_write(*block, &indirect_block);
	return true;
}

bool
alloc_inode_indirect(block_sector_t* block, size_t size)
{
	struct indirect_block indirect_block;
	if (*block == 0) {
		free_map_allocate(1, block);
		buffer_cache_write(*block, empty_page);
	}
	buffer_cache_read(*block, &indirect_block);

	for(size_t i=0; i<size; i++){
		if(!allocate_block(&indirect_block.pointers[i]))
				return false;
	}
	buffer_cache_write(*block, &indirect_block);
	return true;
}

bool
alloc_inode(struct inode_disk *mydisk, off_t file_size)
{
	if (file_size < 0){
		return false;
	}

	size_t size = DIV_ROUND_UP(file_size,BLOCK_SECTOR_SIZE);
	size_t temp;

	temp = size < DIRECT ? size : DIRECT;
	for (size_t i = 0; i < temp; ++i) {
		if (mydisk->direct_blocks[i] == 0) {
			if (!free_map_allocate(1, &mydisk->direct_blocks[i]))
				return false;
			buffer_cache_write(mydisk->direct_blocks[i], empty_page);
		}
	}
	size -= temp;
	if (size == 0)
		return true;

	temp = size < INDIRECT ? size : INDIRECT;
	if (!alloc_inode_indirect(&mydisk->indirect_block, temp))
		return false;
	size -= temp;
	if (size == 0)
		return true;

	temp = size < INDIRECT * INDIRECT ? size : INDIRECT * INDIRECT;
	if (!alloc_inode_doubly_indirect(&mydisk->doubly_indirect_block, temp))
		return false;
	size -= temp;
	if (size == 0)
		return true;

	return false;
}

void
dealloc_inode_doubly_indirect(block_sector_t block, size_t size)
{
	struct indirect_block indirect_block;
	buffer_cache_read(block, &indirect_block);

	for (size_t i = 0; i < DIV_ROUND_UP(size,INDIRECT); ++i) {
		size_t dealloc_length = size < INDIRECT ? size : INDIRECT;
		dealloc_inode_indirect(indirect_block.pointers[i], dealloc_length);
		size -= dealloc_length;
	}

	free_map_release(block, 1);
}
void
dealloc_inode_indirect(block_sector_t block, size_t size)
{
	struct indirect_block indirect_block;
	buffer_cache_read(block, &indirect_block);

	for(size_t i = 0; i<size; i++)
		free_map_release(indirect_block.pointers[i],1);

	free_map_release(block, 1);
}


bool dealloc_inode(struct inode *inode)
{
	if (inode->data.length < 0)
		return false;

	size_t size = DIV_ROUND_UP(inode->data.length,BLOCK_SECTOR_SIZE);

	size_t temp = size < DIRECT ? size : DIRECT;
	for (size_t i = 0; i < temp; ++i) {
		free_map_release(inode->data.direct_blocks[i], 1);
	}
	if(size <= DIRECT)
		return true;

	temp = size < INDIRECT ? size : INDIRECT;
	if (size - DIRECT > 0) {
		dealloc_inode_indirect(inode->data.indirect_block, temp);
	}
	if(size <= DIRECT + INDIRECT)
		return true;

	temp = size <  INDIRECT * INDIRECT ? size : INDIRECT * INDIRECT;
	if (size - DIRECT - INDIRECT > 0) {
		dealloc_inode_doubly_indirect(inode->data.doubly_indirect_block, temp);
	}
	return true;
}

block_sector_t get_sector_number(const struct inode_disk *mydisk, off_t n)
{
	block_sector_t ret;
	struct indirect_block *temp_block;

	if (n < DIRECT){
		return mydisk->direct_blocks[n];
	}
	else if (n < DIRECT + INDIRECT) {
		temp_block = calloc(1, sizeof(struct indirect_block));
		buffer_cache_read(mydisk->indirect_block, temp_block);
		ret = temp_block->pointers[n - DIRECT];
		free(temp_block);

		return ret;
	}
	else if (n < DIRECT + INDIRECT + INDIRECT*INDIRECT) {
		off_t first_level_block = (n - DIRECT - INDIRECT) / INDIRECT;
		off_t second_level_block = (n - DIRECT - INDIRECT) % INDIRECT;

		temp_block = calloc(1, sizeof(struct indirect_block));

		buffer_cache_read(mydisk->doubly_indirect_block, temp_block);
		buffer_cache_read(temp_block->pointers[first_level_block], temp_block);
		ret = temp_block->pointers[second_level_block];

		free(temp_block);
		return ret;
	}

	return -1;
}

bool inode_dir(const struct inode *inode)
{
	return inode->data.is_dir;
}
