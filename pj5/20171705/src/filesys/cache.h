#ifndef CACHE
#define CACHE

#include "devices/block.h"

void buffer_cache_init(void);
void buffer_cache_terminate(void);
struct cache_entry* buffer_cache_select_victim(void);
struct cache_entry* buffer_cache_lookup(block_sector_t sector);
void buffer_cache_flush_entry(struct cache_entry *entry);
void buffer_cache_read(block_sector_t sector, void *buffer);
void buffer_cache_write(block_sector_t sector, const void *buffer);

#endif
