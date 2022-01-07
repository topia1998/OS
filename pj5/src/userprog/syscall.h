#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "userprog/process.h"

void syscall_init (void);

void sys_exit (int);
void sys_halt (void);
void sys_exit (int);
pid_t sys_exec (const char *cmdline);
int sys_wait (pid_t pid);
bool sys_create(const char* filename, unsigned initial_size);
bool sys_remove(const char* filename);
int sys_open(const char* file);
int sys_filesize(int fd);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);

#ifdef VM
// expose munmap() so that it can be call in sys_exit();
bool sys_munmap (mmapid_t);
#endif

#ifdef FILESYS
bool sys_chdir(const char *filename);
bool sys_mkdir(const char *filename);
bool sys_readdir(int fd, char *filename);
bool sys_isdir(int fd);
int sys_inumber(int fd);
#endif


#endif /* userprog/syscall.h */
