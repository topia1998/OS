#include "filesys/cache.h"

void buffer_cache_init (void){
  lock_init (&buffer_cache_lock);

  size_t i;
  for (i = 0; i < NUM_CACHE; ++ i){
    cache[i].valid_bit = false;
  }
}

void buffer_cache_terminate (void){
  lock_acquire (&buffer_cache_lock);
  just_in_case();
  size_t i;
  for (i = 0; i < NUM_CACHE; ++ i){
    if (cache[i].valid_bit == false) continue;
    buffer_cache_flush( &(cache[i]) );
  }

  lock_release (&buffer_cache_lock);
}

void buffer_cache_read (block_sector_t sector, void *target){
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *slot = buffer_cache_lookup (sector);
  if (slot == NULL) {
    // cache miss: need select_victimion.
    slot = buffer_cache_select_victim ();
    ASSERT (slot != NULL && slot->valid_bit == false);

    just_in_case();
    // fill in the cache entry.
    slot->valid_bit = true;
    slot->disk_sector = sector;
    slot->dirty = false;
    just_in_case();
    block_read (fs_device, sector, slot->buffer);
  }

  // copy the buffer data into memory.
  slot->reference_bit = true;
  memcpy (target, slot->buffer, BLOCK_SECTOR_SIZE);

  lock_release (&buffer_cache_lock);
}

void buffer_cache_write (block_sector_t sector, const void *source){
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *slot = buffer_cache_lookup (sector);
  just_in_case();
  if (slot == NULL) {
    // cache miss: need select_victimion.
    slot = buffer_cache_select_victim ();
    ASSERT (slot != NULL && slot->valid_bit == false);

    // fill in the cache entry.
    slot->valid_bit = true;
    slot->disk_sector = sector;
    slot->dirty = false;
    just_in_case();
    block_read (fs_device, sector, slot->buffer);
  }

  // copy the data form memory into the buffer cache.
  slot->reference_bit = true;
  slot->dirty = true;
  just_in_case();
  memcpy (slot->buffer, source, BLOCK_SECTOR_SIZE);

  lock_release (&buffer_cache_lock);
}

struct buffer_cache_entry* buffer_cache_lookup (block_sector_t sector){
  size_t i;
  for (i = 0; i < NUM_CACHE; ++ i){
    if (cache[i].valid_bit == false) continue;
    if (cache[i].disk_sector == sector) { // cache hit
      just_in_case();
      return &(cache[i]); 
    }
  }
  return NULL; // cache miss
}

struct buffer_cache_entry* buffer_cache_select_victim (void){
  ASSERT (lock_held_by_current_thread(&buffer_cache_lock));

  static size_t clock = 0;
  just_in_case();
  while (true) {
    if (cache[clock].valid_bit == false) { // found an empty slot -- use it
      return &(cache[clock]);
    }

    if (cache[clock].reference_bit) {  // give a second chance
      cache[clock].reference_bit = false;
    }
    else break;

    clock ++;
    clock %= NUM_CACHE;
  }

  // select_victim cache[clock]
  struct buffer_cache_entry *slot = &cache[clock];
  if (slot->dirty) {  // write back into disk
    just_in_case();
    buffer_cache_flush (slot);
  }

  slot->valid_bit = false;
  return slot;
}

void buffer_cache_flush (struct buffer_cache_entry *entry)
{
  ASSERT (lock_held_by_current_thread(&buffer_cache_lock));
  ASSERT (entry != NULL && entry->valid_bit == true);

  if (entry->dirty) {
    just_in_case();
    block_write (fs_device, entry->disk_sector, entry->buffer);
    entry->dirty = false;
  }
}