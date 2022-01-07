#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdio.h>
#include <string.h>
#include <debug.h>
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "devices/block.h"
#include "threads/thread.h"

struct buffer_cache_entry {
  bool valid_bit;
  bool reference_bit;
  bool dirty;     

  block_sector_t disk_sector;
  uint8_t buffer[BLOCK_SECTOR_SIZE]; // 512 * 1B
};
#define NUM_CACHE 64
static struct buffer_cache_entry cache[NUM_CACHE];
static struct lock buffer_cache_lock;

void buffer_cache_init (void);
void buffer_cache_terminate (void);
void buffer_cache_read (block_sector_t sector, void *target);
void buffer_cache_write (block_sector_t sector, const void *source);
struct buffer_cache_entry *buffer_cache_lookup (block_sector_t);
struct buffer_cache_entry *buffer_cache_select_victim (void);
void buffer_cache_flush(struct buffer_cache_entry*);

#endif