#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

#define WORD sizeof(uint32_t)

static void syscall_handler (struct intr_frame *);

static void check_user (const uint8_t *uaddr);
static int32_t get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static int memread_user (void *src, void *des, size_t bytes);

enum fd_search_filter { FD_FILE = 1, FD_DIRECTORY = 2 };
static struct file_desc* find_file_desc(struct thread *, int fd, enum fd_search_filter flag);
int fibonacci(int n);
int max_of_four_int(int a, int b, int c, int d);

struct lock filesys_lock;

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// in case of invalid memory access, fail and exit.
static void fail_invalid_access(void) {
  if (lock_held_by_current_thread(&filesys_lock))
    lock_release (&filesys_lock);

  sys_exit (-1);
  NOT_REACHED();
}

static void
syscall_handler (struct intr_frame *f)
{

  uint32_t syscall_number = *(uint32_t *)(f->esp);
  void *f_esp = f->esp;

  switch (syscall_number) {
  case SYS_HALT: // 0
      sys_halt();
      break;
    
  case SYS_EXIT: // 1
      if (!is_user_vaddr (f_esp + WORD))
        sys_exit (-1);
      sys_exit((int)*(uint32_t *) (f_esp + WORD));
      break;

  case SYS_EXEC: // 2
      if (!is_user_vaddr (f_esp + WORD))
        sys_exit (-1);
      f->eax = sys_exec ((const char*) *(uint32_t *) (f_esp + WORD));
      break;
  
  case SYS_WAIT: // 3
      if (!is_user_vaddr (f_esp + WORD))
        sys_exit (-1);
      f->eax = sys_wait ((pid_t) *(uint32_t *) (f_esp + WORD));
      break;
    
  case SYS_CREATE: // 4
      if (!is_user_vaddr (f_esp + WORD)|| !is_user_vaddr (f_esp + 2*WORD))
        sys_exit (-1);
      f->eax = sys_create ((const char*) *(uint32_t *) (f_esp + WORD), (unsigned) *((uint32_t *) (f_esp + 2*WORD)));
      break;

  case SYS_REMOVE: // 5
      if (!is_user_vaddr (f_esp + WORD))
        sys_exit(-1);
      f->eax = sys_remove ((const char*) *(uint32_t *) (f_esp + WORD));
      break;
    
  case SYS_OPEN: // 6
      if (!is_user_vaddr (f_esp + WORD))
        sys_exit(-1);
      f->eax = sys_open ((const char*) *(uint32_t *) (f_esp + WORD));
      break;
    
  case SYS_FILESIZE: // 7
      if (!is_user_vaddr (f_esp + WORD))
        sys_exit(-1);
      f->eax = sys_filesize ((int) *(uint32_t *) (f_esp + WORD));
      break;
    
  case SYS_READ: // 8
      if (!is_user_vaddr (f_esp + WORD)|| !is_user_vaddr (f_esp + 2*WORD)|| !is_user_vaddr (f_esp + 3*WORD))
        sys_exit (-1);
      f->eax = sys_read ((int) *(uint32_t *) (f_esp + WORD), (void *) *(uint32_t *) (f_esp + 2*WORD), (unsigned) *((uint32_t *) (f_esp + 3*WORD)));
      break;
    
  case SYS_WRITE: // 9
      if (!is_user_vaddr (f_esp + WORD)|| !is_user_vaddr (f_esp + 2*WORD)|| !is_user_vaddr (f_esp + 3*WORD))
        sys_exit (-1);
      f->eax = sys_write ((int) *(uint32_t *) (f_esp + WORD),(void*) *(uint32_t *) (f_esp + 2*WORD),(unsigned) *((uint32_t *) (f_esp + 3*WORD)));
      break;
  
  case SYS_SEEK: // 10
      if(!is_user_vaddr (f_esp + WORD)|| !is_user_vaddr (f_esp + 2*WORD))
        sys_exit(-1);
      sys_seek ((int) *(uint32_t *) (f_esp + WORD), (unsigned) *((uint32_t *) (f_esp + 2*WORD)));
      break;
    
  case SYS_TELL: // 11
      if(!is_user_vaddr (f_esp + WORD))
        sys_exit(-1);
      f->eax = sys_tell ((int) *(uint32_t *) (f_esp + WORD));
      break;
    
  case SYS_CLOSE: // 12
      if(!is_user_vaddr (f_esp + WORD))
        sys_exit(-1);
      sys_close ((int) *(uint32_t *) (f_esp + WORD));
      break;

#ifdef FILESYS
  case SYS_CHDIR: // 15
    {
      const char* filename;
      int return_code;

      memread_user(f->esp + 4, &filename, sizeof(filename));

      return_code = sys_chdir(filename);
      f->eax = return_code;
      break;
    }

  case SYS_MKDIR: // 16
    {
      const char* filename;
      int return_code;

      memread_user(f->esp + 4, &filename, sizeof(filename));

      return_code = sys_mkdir(filename);
      f->eax = return_code;
      break;
    }

  case SYS_READDIR: // 17
    {
      int fd;
      char *name;
      int return_code;

      memread_user(f->esp + 4, &fd, sizeof(fd));
      memread_user(f->esp + 8, &name, sizeof(name));

      return_code = sys_readdir(fd, name);
      f->eax = return_code;
      break;
    }

  case SYS_ISDIR: // 18
    {
      int fd;
      int return_code;

      memread_user(f->esp + 4, &fd, sizeof(fd));
      return_code = sys_isdir(fd);
      f->eax = return_code;
      break;
    }

  case SYS_INUMBER: // 19
    {
      int fd;
      int return_code;

      memread_user(f->esp + 4, &fd, sizeof(fd));
      return_code = sys_inumber(fd);
      f->eax = return_code;
      break;
    }
#endif
  }
}

/****************** System Call Implementations ********************/

void sys_halt(void) {
  shutdown_power_off();
}

void sys_exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);

  struct process_control_block *pcb = thread_current()->pcb;
  if(pcb != NULL) {
    pcb->exitcode = status;
  }

  thread_exit();
}

pid_t sys_exec(const char *cmdline) { 
  return process_execute(cmdline);
}

int sys_wait(pid_t pid) {
  return process_wait(pid);
}

bool sys_create(const char* filename, unsigned initial_size) {
  if(!is_user_vaddr(filename) || filename == NULL)
    sys_exit(-1);
  
  return filesys_create(filename, initial_size, false);
}

bool sys_remove(const char* filename) {
  if(!is_user_vaddr(filename) || filename == NULL)
      sys_exit(-1);

  return filesys_remove(filename);
}

int sys_open(const char* file) {
  // memory validation
  check_user((const uint8_t*) file);

  struct file* file_opened;
  struct file_desc* fd = palloc_get_page(0);
  if (!fd) {
    return -1;
  }

  lock_acquire (&filesys_lock);
  file_opened = filesys_open(file);
  if (!file_opened) {
    palloc_free_page (fd);
    lock_release (&filesys_lock);
    return -1;
  }

  fd->file = file_opened; //file save

  // directory handling
  struct inode *inode = file_get_inode(fd->file);
  if(inode != NULL && inode_is_directory(inode)) {
    fd->dir = dir_open( inode_reopen(inode) );
  }
  else fd->dir = NULL;

  struct list* fd_list = &thread_current()->file_descriptors;
  if (list_empty(fd_list)) {
    // 0, 1, 2 are reserved for stdin, stdout, stderr
    fd->id = 3;
  }
  else {
    fd->id = (list_entry(list_back(fd_list), struct file_desc, elem)->id) + 1;
  }
  list_push_back(fd_list, &(fd->elem));

  lock_release (&filesys_lock);
  return fd->id;
}

int sys_filesize(int fd) {
  struct file_desc* file_d = find_file_desc(thread_current(), fd, FD_FILE);
  return file_length(file_d->file);
}

void sys_seek(int fd, unsigned position) {
  struct file_desc* file_d = find_file_desc(thread_current(), fd, FD_FILE);

  if(file_d && file_d->file) {
    file_seek(file_d->file, position);
  }
  else
    return; 

}

unsigned sys_tell(int fd) {
  struct file_desc* file_d = find_file_desc(thread_current(), fd, FD_FILE);

  unsigned ret;
  if(file_d && file_d->file) {
    ret = file_tell(file_d->file);
  }
  else
    ret = -1; 

  return ret;
}

void sys_close(int fd) {
  struct file_desc* file_d = find_file_desc(thread_current(), fd, FD_FILE | FD_DIRECTORY);

  if(file_d && file_d->file) {
    file_close(file_d->file);
    if(file_d->dir) dir_close(file_d->dir);
    list_remove(&(file_d->elem));
    palloc_free_page(file_d);
  }
}

int sys_read(int fd, void *buffer, unsigned size) {
  // memory validation : [buffer+0, buffer+size) should be all valid
  check_user((const uint8_t*) buffer);
  check_user((const uint8_t*) buffer + size - 1);

  lock_acquire (&filesys_lock);
  int ret;

  if(fd == 0) { // stdin
    unsigned i;
    for(i = 0; i < size; ++i) {
      if(! put_user(buffer + i, input_getc()) ) {
        lock_release (&filesys_lock);
        sys_exit(-1); // segfault
      }
    }
    ret = size;
  }
  else {
    // read from file
    struct file_desc* file_d = find_file_desc(thread_current(), fd, FD_FILE);

    if(file_d && file_d->file) {

      ret = file_read(file_d->file, buffer, size);

    }
    else // no such file or can't open
      ret = -1;
  }

  lock_release (&filesys_lock);
  return ret;
}

int sys_write(int fd, const void *buffer, unsigned size) {
  // memory validation : [buffer+0, buffer+size) should be all valid
  check_user((const uint8_t*) buffer);
  check_user((const uint8_t*) buffer + size - 1);

  lock_acquire (&filesys_lock);
  int ret;

  if(fd == 1) { // write to stdout
    putbuf(buffer, size);
    ret = size;
  }
  else {
    // write into file
    struct file_desc* file_d = find_file_desc(thread_current(), fd, FD_FILE);

    if(file_d && file_d->file) {

      ret = file_write(file_d->file, buffer, size);

    }
    else // no such file or can't open
      ret = -1;
  }

  lock_release (&filesys_lock);
  return ret;
}

int fibonacci(int n)
{
    int res = 0;
    int f1 = 1, f2 = 1;
    int i;

    if (n == 1 || n == 2)
        res = 1;
    else
        for (i = 0; i < n - 2; ++i)
        {
            res = f1 + f2;
            f1 = f2;
            f2 = res;
        }

    printf("%d ", res);
    return res;
}

int max_of_four_int(int a, int b, int c, int d)
{
    int max = 0;

    max = (a>b) ? a : b;
    max = (max>c) ? max : c;
    max = (max>d) ? max : d;

    printf("%d\n", max);
    return max;
}
/****************** Helper Functions on Memory Access ********************/

static void
check_user (const uint8_t *uaddr) {
  // check uaddr range or segfaults
  if(get_user (uaddr) == -1)
    fail_invalid_access();
}

static int32_t
get_user (const uint8_t *uaddr) {
  // check that a user pointer `uaddr` points below PHYS_BASE
  if (! ((void*)uaddr < PHYS_BASE)) {
    return -1;
  }

  // as suggested in the reference manual, see (3.1.5)
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));
  return result;
}

static bool
put_user (uint8_t *udst, uint8_t byte) {
  // check that a user pointer `udst` points below PHYS_BASE
  if (! ((void*)udst < PHYS_BASE)) {
    return false;
  }

  int error_code;

  // as suggested in the reference manual, see (3.1.5)
  asm ("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static int
memread_user (void *src, void *dst, size_t bytes)
{
  int32_t value;
  size_t i;
  for(i=0; i<bytes; i++) {
    value = get_user(src + i);
    if(value == -1) // segfault or invalid memory access
      fail_invalid_access();

    *(char*)(dst + i) = value & 0xff;
  }
  return (int)bytes;
}

/**************************************/

static struct file_desc*
find_file_desc(struct thread *t, int fd, enum fd_search_filter flag)
{
  ASSERT (t != NULL);

  if (fd < 3) {
    return NULL;
  }

  struct list_elem *e;

  if (! list_empty(&t->file_descriptors)) {
    for(e = list_begin(&t->file_descriptors);
        e != list_end(&t->file_descriptors); e = list_next(e))
    {
      struct file_desc *desc = list_entry(e, struct file_desc, elem);
      if(desc->id == fd) {
        // found. filter by flag to distinguish file and directorys
        if (desc->dir != NULL && (flag & FD_DIRECTORY) )
          return desc;
        else if (desc->dir == NULL && (flag & FD_FILE) )
          return desc;
      }
    }
  }

  return NULL; // not found
}

#ifdef FILESYS

bool sys_chdir(const char *filename)
{
  bool return_code;
  check_user((const uint8_t*) filename);

  just_in_case();
  lock_acquire (&filesys_lock);
  return_code = filesys_chdir(filename);
  lock_release (&filesys_lock);

  return return_code;
}

bool sys_mkdir(const char *filename)
{
  bool return_code;
  check_user((const uint8_t*) filename);

  just_in_case();
  lock_acquire (&filesys_lock);
  return_code = filesys_create(filename, 0, true);
  lock_release (&filesys_lock);

  return return_code;
}

bool sys_readdir(int fd, char *name)
{
  struct file_desc* file_d;
  bool ret = false;

  just_in_case();
  lock_acquire (&filesys_lock);
  file_d = find_file_desc(thread_current(), fd, FD_DIRECTORY);
  if (file_d == NULL) goto done;

  struct inode *inode;
  inode = file_get_inode(file_d->file); // file descriptor -> inode
  if(inode == NULL) goto done;

  // check whether it is a valid directory
  if(! inode_is_directory(inode)) goto done;

  ASSERT (file_d->dir != NULL); // see sys_open()
  ret = dir_readdir (file_d->dir, name);

done:
  lock_release (&filesys_lock);
  return ret;
}

bool sys_isdir(int fd)
{
  just_in_case();
  lock_acquire (&filesys_lock);

  struct file_desc* file_d = find_file_desc(thread_current(), fd, FD_FILE | FD_DIRECTORY);
  bool ret = inode_is_directory (file_get_inode(file_d->file));

  lock_release (&filesys_lock);
  return ret;
}

int sys_inumber(int fd)
{
  just_in_case();
  lock_acquire (&filesys_lock);

  struct file_desc* file_d = find_file_desc(thread_current(), fd, FD_FILE | FD_DIRECTORY);
  int ret = (int) inode_get_inumber (file_get_inode(file_d->file));

  lock_release (&filesys_lock);
  return ret;
}

#endif