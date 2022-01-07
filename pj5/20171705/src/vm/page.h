#ifndef PAGE_HEADER
#define PAGE_HEADER
#include <hash.h>
#include <threads/thread.h>

struct spt_e{
	void *vaddr;
	void *kpage;
	size_t page_read_bytes;
	size_t page_zero_bytes;
	bool writable;
	struct file* file;
	struct hash_elem elem;
	size_t ofs;
	int swap_slot;
};

unsigned hash_value(const struct hash_elem* e,void *aux);

bool hash_compare(const struct hash_elem *a,const struct hash_elem *b,void *aux);

void spte_destroy(struct hash_elem *elem,void *aux);

void add_spte(void* upage,void* kpage,size_t page_read_bytes,size_t page_zero_bytes,bool writable,struct file* file,size_t ofs);

#endif
