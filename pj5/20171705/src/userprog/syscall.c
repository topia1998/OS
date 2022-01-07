#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static struct lock filesys_lock;

#ifdef FILESYS
bool chdir(const char *filename);
bool mkdir(const char *filename);
bool readdir(int fd, char *filename);
bool isdir(int fd);
int inumber(int fd);
#endif

struct list_item* get_fd(struct thread*,int fd,bool directory, bool file);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_no = *(int*)(f->esp);

  if(!is_user_vaddr(f->esp)){
	  exit(-1);
  }

  if(syscall_no == SYS_HALT){
	  halt();
  }
  else if(syscall_no == SYS_EXIT){
	  if(!is_user_vaddr(f->esp + 4))
		  exit(-1);
	exit(*(int*)(f->esp + 4));
  }
  else if(syscall_no == SYS_EXEC){
	  if(!is_user_vaddr(f->esp + 4))
		  exit(-1);
	 f->eax = exec(*(int*)(f->esp + 4));
  }
  else if(syscall_no == SYS_WAIT){
	  if(!is_user_vaddr(f->esp + 4))
		  exit(-1);
	f->eax = wait(*(int*)(f->esp + 4));
  }
  else if(syscall_no == SYS_READ){
	  if(!is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || !is_user_vaddr(f->esp + 12))
		  exit(-1);
	f->eax = read(*(int*)(f->esp+4),*(int*)(f->esp+8),*(unsigned*)(f->esp+12));
  }
  else if(syscall_no == SYS_WRITE){
	  if(!is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || !is_user_vaddr(f->esp + 12))
		  exit(-1);
	f->eax = write(*(int*)(f->esp + 4),*(int*)(f->esp+8),*(unsigned*)(f->esp+12));
  }
  else if(syscall_no == SYS_FIBONACCI){
	if(!is_user_vaddr(f->esp + 4))
		exit(-1);
	f->eax = fibonacci(*(int*)(f->esp + 4));

  }
  else if(syscall_no == SYS_MAXOFFOURINT){
	  if(!is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || !is_user_vaddr(f->esp + 12) ||
			  !is_user_vaddr(f->esp + 16))
		  exit(-1);
	f->eax = max_of_four_int(*(int*)(f->esp+4),*(int*)(f->esp+8),*(int*)(f->esp+12),*(int*)(f->esp+16));
  }
  else if(syscall_no == SYS_CREATE){
	if(!is_user_vaddr(f->esp+4) || !is_user_vaddr(f->esp + 8))
		exit(-1);

	  f->eax = create(*(const char**)(f->esp+4),*(unsigned*)(f->esp+8));
  }
  else if(syscall_no == SYS_REMOVE){
	  if(!is_user_vaddr(f->esp + 4))
		  exit(-1);
	  f->eax = remove(*(const char**)(f->esp+4));
  }
  else if(syscall_no == SYS_OPEN){
	  if(!is_user_vaddr(f->esp + 4))
		  exit(-1);
	  f->eax = open(*(const char**)(f->esp+4));
  }
  else if(syscall_no == SYS_FILESIZE){
	  if(!is_user_vaddr(f->esp + 4))
		  exit(-1);
	  f->eax = filesize(*(int*)(f->esp+4));
  }
  else if(syscall_no == SYS_SEEK){
	  if(!is_user_vaddr(f->esp + 4)||!is_user_vaddr(f->esp+8))
		  exit(-1);
	  seek(*(int*)(f->esp+4),*(int*)(f->esp+8));
  }
  else if(syscall_no == SYS_TELL){
	  if(!is_user_vaddr(f->esp+4))
		 exit(-1);
	  f->eax = tell(*(int*)(f->esp+4));
  }
  else if(syscall_no == SYS_CLOSE){
	  if(!is_user_vaddr(f->esp + 4))
		  exit(-1); 
	  close(*(int*)(f->esp+4));
  }
#ifdef FILESYS
  else if(syscall_no ==  SYS_CHDIR)
  {
	  int return_code;

	  if (!is_user_vaddr(f->esp + 4))
		  exit(-1);

	  f->eax = chdir(*(int*)(f->esp + 4));
  }

  else if(syscall_no == SYS_MKDIR)
  {
	  int return_code;

	  if (!is_user_vaddr(f->esp + 4))
		  exit(-1);

	  f->eax = mkdir(*(int*)(f->esp + 4));

  }

  else if(syscall_no == SYS_READDIR)
  {
	  int return_code;

	  if (!is_user_vaddr(f->esp + 4)|| !is_user_vaddr(f->esp + 8))
		  exit(-1);
	  f->eax = readdir(*(int*)(f->esp + 4), *(int*)(f->esp + 8));
  }

  else if(syscall_no == SYS_ISDIR)
  {
	  int return_code;

	  if (!is_user_vaddr(f->esp + 4))
		  exit(-1);

	  f->eax = isdir(*(int*)(f->esp + 4));
  }

  else if(syscall_no ==SYS_INUMBER)
  {
	  int return_code;

	  if (!is_user_vaddr(f->esp + 4))
		  exit(-1);

	  f->eax = inumber(*(int*)(f->esp + 4));
  }

#endif

  //thread_exit ();
}

void halt(){
  shutdown_power_off();
}
void exit(int exit_number){

  thread_current()->exit_number = exit_number;
  printf("%s: exit(%d)\n",thread_current()->name,exit_number);
  thread_exit();

}
int exec(const char* filename){
	 int pid =  process_execute(filename);
	 return pid;
}
int wait(int pid){
	return  process_wait(pid);
}
int read(int fd,int *buffer,unsigned size){
	
	if(!is_user_vaddr(buffer)){
		exit(-1);
	}
	if(fd == 0){
		for(int i=0; i<size; i++){
			*buffer = input_getc();
			if(*buffer != '\0')
				buffer++;
			else{ 
				size = i;
				break;
			}
		}
		return size; 
	}
	else{
		struct list_elem* elem;
		struct list_item* item;
		unsigned r_size = 0;
		for(elem = list_begin(&(thread_current()->file_list));
			elem != list_end(&(thread_current()->file_list)); elem = list_next(elem)){
			if((item = list_entry(elem,struct list_item,elem))->fd == fd){
				file_read_sema_down(item->f);
				r_size =  file_read(item->f,buffer,size);
				file_read_sema_up(item->f);
				return r_size;
			}
		}
		return 0;
	}
}
int write(int fd,int *buffer,unsigned size){
	if(!is_user_vaddr(buffer)){
		exit(-1);
	}
	if(fd == 1){
		putbuf((const char*)buffer,size);
		return size;
	}
	else{
		
		struct list_elem* elem;
		struct list_item* item;
		unsigned w_size = 0;
		for(elem = list_begin(&(thread_current()->file_list));
			elem != list_end(&(thread_current()->file_list)); elem = list_next(elem)){
			if((item = list_entry(elem,struct list_item,elem))->fd == fd){
				
				if(item->dir != NULL){
					return -1;
				}
				else{
					file_write_sema_down(item->f);
					w_size = file_write(item->f,buffer,size);
					file_write_sema_up(item->f);
					return  w_size;
					
				}
			}
		}
		return 0;

	}
	
}
int fibonacci(int n){
	if( n <= 0)
		return -1;
	else if(n <= 2)
		return 1;
	else 
		return fibonacci(n-1) + fibonacci(n-2);
}

int max_of_four_int(int a,int b,int c,int d){
	int retval = a;
	if(retval < b)
		retval = b;
	if(retval < c)
		retval = c;
	if(retval < d)
		retval = d;

	return retval;
}
int create(const char* file, unsigned initial_size){
	int ret; 
	if(file == NULL){
		exit(-1);
	}
	//lock_acquire(&filesys_lock);
	ret = filesys_create(file,initial_size,false);
	//lock_release(&filesys_lock);
	return ret;
}
int remove(const char* file){
	//lock_acquire(&filesys_lock);
	int ret = filesys_remove(file);
	//lock_release(&filesys_lock);

	return ret;
}
int open(const char* file){

	if(file == NULL){
		return -1;
	}
	//lock_acquire(&filesys_lock);
	struct file* f = filesys_open(file);
	int fd = -1;
	if(f == NULL){
	 	//lock_release(&filesys_lock);
		return -1;
	}

	if(thread_current()->file_bitmap == NULL){
		thread_current()->file_bitmap = bitmap_create(512);
		bitmap_set(thread_current()->file_bitmap,0,true);
		bitmap_set(thread_current()->file_bitmap,1,true);
	}
	
	for(int i=2; i<=511; i++){
		if(bitmap_count(thread_current()->file_bitmap,i,1,false) == 1){
			fd = i;
			bitmap_set(thread_current()->file_bitmap,i,true);
			break;
		}
	}


	struct list_item* item = (struct list_item*)malloc(sizeof(struct list_item));
	item->fd = fd;
	item->f = f;
	
	struct inode *inode = file_get_inode(item->f);
	if(inode != NULL && inode_dir(inode)){
		item->dir = dir_open(inode_reopen(inode));
	}
	else item->dir = NULL;

	list_push_back(&(thread_current()->file_list),&(item->elem));
	
	if(thread_findname_foreach(file))
		file_deny_write(item->f);

	//lock_release(&filesys_lock);
	return fd;
}
int filesize(int fd){
	if(bitmap_count(thread_current()->file_bitmap,fd,1,false) == 1){
		return 0;
	}
	struct list_elem* elem;
	struct list_item* item;
	for(elem = list_begin(&(thread_current()->file_list));
		elem != list_end(&(thread_current()->file_list)); elem = list_next(elem)){
		if((item = list_entry(elem,struct list_item,elem))->fd == fd){
			return file_length(item->f);
		}
	}
	return 0;
}
void seek(int fd, unsigned position){
	if(bitmap_count(thread_current()->file_bitmap,fd,1,false) == 1){
		return ;
	}
	struct list_elem* elem;
	struct list_item* item;
	for(elem = list_begin(&(thread_current()->file_list));
		elem != list_end(&(thread_current()->file_list)); elem = list_next(elem)){
		if((item = list_entry(elem,struct list_item,elem))->fd == fd){
			file_seek(item->f,position);
			break;
		}
	}

	return;
}
unsigned tell(int fd){
	if(bitmap_count(thread_current()->file_bitmap,fd,1,false) == 1){
		return 0;
	}
	struct list_elem* elem;
	struct list_item* item;
	for(elem = list_begin(&(thread_current()->file_list));
		elem != list_end(&(thread_current()->file_list)); elem = list_next(elem)){
		if((item = list_entry(elem,struct list_item,elem))->fd == fd){
			return file_tell(item->f);
		}
	}
	return 0;
}
void close(int fd){
	//lock_acquire(&filesys_lock);

	if(thread_current()->file_bitmap == NULL){
		thread_current()->file_bitmap = bitmap_create(512);
		bitmap_set(thread_current()->file_bitmap,0,true);
		bitmap_set(thread_current()->file_bitmap,1,true);
	}

	//temporary code in project 2
	if(fd == 0 || fd == 1){
		bitmap_set(thread_current()->file_bitmap,fd,false);
	}

	struct list_elem* elem;
	struct list_item* item;
	for(elem = list_begin(&(thread_current()->file_list));
		elem != list_end(&(thread_current()->file_list)); elem = list_next(elem)){
		if((item = list_entry(elem,struct list_item,elem))->fd == fd){
			file_close(item->f);
			if(item->dir)
				dir_close(item->dir);
			bitmap_set(thread_current()->file_bitmap,item->fd,false);
			list_remove(elem);
			break;
		}
	}
	//lock_release(&filesys_lock);
	return;
}

#ifdef FILESYS

bool chdir(const char *fname)
{
	bool ret;

	lock_acquire(&filesys_lock);

	ret = filesys_chdir(fname);

	lock_release(&filesys_lock);

	return ret;
}

bool mkdir(const char *fname)
{
	bool ret;

	lock_acquire(&filesys_lock);

	ret = filesys_create(fname, 0, true);

	lock_release(&filesys_lock);

	return ret;
}

bool readdir(int fd, char *fname)
{
	struct list_item* item;
	bool ret = false;

	lock_acquire(&filesys_lock);

	item = get_fd(thread_current(), fd, true,false);

	if (item == NULL){
		lock_release(&filesys_lock);
		return false;
	}

	struct inode *inode;
	inode = file_get_inode(item->f);

	if (inode == NULL){
		lock_release(&filesys_lock);
		return false;
	}

	if (!inode_dir(inode)){
		lock_release(&filesys_lock);
		return false;
	}

	ret = dir_readdir(item->dir, fname);

	lock_release(&filesys_lock);
	return ret;
}

bool isdir(int fd)
{
	lock_acquire(&filesys_lock);

	struct list_item* file_d = get_fd(thread_current(), fd, true,true);
	bool ret = inode_dir(file_get_inode(file_d->f));

	lock_release(&filesys_lock);
	return ret;
}

int inumber(int fd)
{
	lock_acquire(&filesys_lock);

	struct list_item* item = NULL;
	struct thread* t = thread_current();

	struct list_elem *e;
	if(!list_empty(&t->file_list)){
		for(e = list_begin(&t->file_list); e != list_end(&t->file_list); e = list_next(e))
		{
			struct list_item *temp_item = list_entry(e,struct list_item,elem);
			if(temp_item->fd == fd){
				if(item->dir != NULL ){
					item = temp_item;
					break;
				}
				else if(item->dir == NULL){
					item =  temp_item;
					break;
				}
			}
		}
	}
	int ret = (int)inode_get_inumber(file_get_inode(item->f));
	lock_release(&filesys_lock);
	return ret;
}
#endif

struct list_item* get_fd(struct thread *t,int fd,bool directory,bool file)
{
	if(fd <= 2)
		return NULL;

	struct list_elem *e;
	if(list_empty(&t->file_list))
		return NULL;

	for(e = list_begin(&t->file_list); e != list_end(&t->file_list); e = list_next(e))
	{
		struct list_item *item = list_entry(e,struct list_item,elem);
		if(item->fd == fd){
			if(item->dir != NULL && directory == true )
			{
				return item;
			}
			else if(item->dir == NULL && file == true )
			{
				return item;
			}
		}
	}
	return NULL;
}
