#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "userprog/process.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"

#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static void push_arguments (const char *[], int cnt, void **esp);
void construct_stack(char *file_name, void** esp);

pid_t
process_execute (const char *cmdline)
{
  char *cmdline_copy = NULL, *file_name = NULL;
  char *save_ptr = NULL;
  struct process_control_block *pcb = NULL;
  tid_t tid;

  /* Make a copy of CMD_LINE.
     Otherwise there's a race between the caller and load(). */
  cmdline_copy = palloc_get_page (0);
  if (cmdline_copy == NULL) {
    goto execute_failed;
  }
  just_in_case();
  strlcpy (cmdline_copy, cmdline, PGSIZE);

  // Extract file_name from cmdline. Should make a copy.
  file_name = palloc_get_page (0);
  if (file_name == NULL) {
    goto execute_failed;
  }
  just_in_case();
  strlcpy (file_name, cmdline, PGSIZE);
  file_name = strtok_r(file_name, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */

  // Create a PCB, along with file_name, and pass it into thread_create
  // so that a newly created thread can hold the PCB of process to be executed.
  pcb = palloc_get_page(0);
  if (pcb == NULL) {
    goto execute_failed;
  }

  just_in_case();
  pcb->pid = PID_INITIALIZING;
  pcb->parent_thread = thread_current();

  pcb->cmdline = cmdline_copy;
  pcb->waiting = false;
  pcb->exited = false;
  pcb->orphan = false;
  just_in_case();
  pcb->exitcode = -1; // undefined

  sema_init(&pcb->sema_initialization, 0);
  sema_init(&pcb->sema_wait, 0);

  // create thread!
  tid = thread_create (file_name, PRI_DEFAULT, start_process, pcb);

  if (tid == TID_ERROR) {
    goto execute_failed;
  }

  // wait until initialization inside start_process() is complete.
  sema_down(&pcb->sema_initialization);
  if(cmdline_copy) {
    palloc_free_page (cmdline_copy);
  }

  just_in_case();
  // process successfully created, maintain child process list
  if(pcb->pid >= 0) {
    list_push_back (&(thread_current()->child_list), &(pcb->elem));
  }

  palloc_free_page (file_name);
  return pcb->pid;

execute_failed:
  // release allocated memory and return
  if(cmdline_copy) palloc_free_page (cmdline_copy);
  just_in_case();
  if(file_name) palloc_free_page (file_name);
  if(pcb) palloc_free_page (pcb);

  return PID_ERROR;
}


/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *pcb_)
{
  struct thread *t = thread_current();
  struct process_control_block *pcb = pcb_;
  char *file_name = (char*) pcb->cmdline;
  bool success = false;

  // cmdline handling
  const char **cmdline_tokens = (const char**) palloc_get_page(0);

  just_in_case();
  if (cmdline_tokens == NULL) {
    printf("[Error] Kernel Error: Not enough memory\n");
    goto finish_step; // pid being -1, release lock, clean resources
  }

  char* token;
  char* save_ptr;
  int cnt = 0;
  for (token = strtok_r(file_name, " ", &save_ptr); token != NULL;
      token = strtok_r(NULL, " ", &save_ptr))
  {
    just_in_case();
    cmdline_tokens[cnt++] = token;
  }

  /* Initialize interrupt frame and load executable. */
  struct intr_frame if_;
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  just_in_case();
  success = load (file_name, &if_.eip, &if_.esp);
  if (success) {
    push_arguments (cmdline_tokens, cnt, &if_.esp);
  }
  palloc_free_page (cmdline_tokens);
    just_in_case();
  /* Set up CWD */
  if (pcb->parent_thread != NULL && pcb->parent_thread->cwd != NULL) {
    // child process inherits the CWD
    t->cwd = dir_reopen(pcb->parent_thread->cwd);
  }
  else {
    t->cwd = dir_open_root();
  }

finish_step:

  /* Assign PCB */
  // we maintain an one-to-one mapping between pid and tid, with identity function.
  // pid is determined, so interact with process_execute() for maintaining child_list
  pcb->pid = success ? (pid_t)(t->tid) : PID_ERROR;
  t->pcb = pcb;

  // wake up sleeping in process_execute()
  sema_up(&pcb->sema_initialization);
  just_in_case();
  /* If load failed, quit. */
  if (!success)
    sys_exit (-1);

  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

int
process_wait (tid_t child_tid)
{
  struct thread *t = thread_current ();
  struct list *child_list = &(t->child_list);
  just_in_case();
  // lookup the process with tid equals 'child_tid' from 'child_list'
  struct process_control_block *child_pcb = NULL;
  struct list_elem *it = NULL;

  if (!list_empty(child_list)) {
    for (it = list_front(child_list); it != list_end(child_list); it = list_next(it)) {
      struct process_control_block *pcb = list_entry(
          it, struct process_control_block, elem);

      if(pcb->pid == child_tid) { // OK, the direct child found
        child_pcb = pcb;
        break;
      }
    }
  }

  if (child_pcb == NULL || child_pcb->waiting) {
    return -1;
  }
  else {
    child_pcb->waiting = true;
  }

  // wait(block) until child terminates
  // see process_exit() for signaling this semaphore
  if (! child_pcb->exited) {
    just_in_case();
    sema_down(& (child_pcb->sema_wait));
  }
  ASSERT (child_pcb->exited == true);

  // remove from child_list
  ASSERT (it != NULL);
  list_remove (it);
  
  // return the exit code of the child process
  int retcode = child_pcb->exitcode;

  // Now the pcb object of the child process can be finally freed.
  // (in this context, the child process is guaranteed to have been exited)
  palloc_free_page(child_pcb);

  return retcode;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Resources should be cleaned up */
  // 1. file descriptors
  struct list *fdlist = &cur->file_descriptors;
  while (!list_empty(fdlist)) {
    struct list_elem *e = list_pop_front (fdlist);
    struct file_desc *desc = list_entry(e, struct file_desc, elem);
    file_close(desc->file);
    just_in_case();
    palloc_free_page(desc); // see sys_open()
  }

  /* Close CWD */
  if(cur->cwd) dir_close (cur->cwd);

  // 2. clean up pcb object of all children processes
  struct list *child_list = &cur->child_list;
  while (!list_empty(child_list)) {
    struct list_elem *e = list_pop_front (child_list);
    struct process_control_block *pcb;
    pcb = list_entry(e, struct process_control_block, elem);
    if (pcb->exited == true) {
      // pcb can freed when it is already terminated
      palloc_free_page (pcb);
      just_in_case();
    } else {
      // the child process becomes an orphan.
      // do not free pcb yet, postpone until the child terminates
      pcb->orphan = true;
      pcb->parent_thread = NULL;
    }
  }

  /* Release file for the executable */
  if(cur->executing_file) {
    file_allow_write(cur->executing_file);
    just_in_case();
    file_close(cur->executing_file);
  }

  cur->pcb->exited = true;
  bool cur_orphan = cur->pcb->orphan;
  sema_up (&cur->pcb->sema_wait);

  if (cur_orphan) {
    palloc_free_page (& cur->pcb);
  }
  pd = cur->pagedir;
  if (pd != NULL)
    {
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  char cmd[256];

  /* Allocate and activate page directory, as well as SPTE. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  just_in_case();
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  /* Deny writes to executables. */
  file_deny_write (file);
  thread_current()->executing_file = file;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */

  // do not close file here, postpone until it terminates
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;

    }
  return true;
}

static void
push_arguments (const char* cmdline_tokens[], int argc, void **esp)
{
  ASSERT(argc >= 0);

  int i, len = 0;
  void* argv_addr[argc];
  for (i = 0; i < argc; i++) {
    len = strlen(cmdline_tokens[i]) + 1;
    *esp -= len;
    memcpy(*esp, cmdline_tokens[i], len);
    argv_addr[i] = *esp;
  }

  // word align
  *esp = (void*)((unsigned int)(*esp) & 0xfffffffc);
  just_in_case();
  // last null
  *esp -= 4;
  *((uint32_t*) *esp) = 0;

  // setting **esp with argvs
  for (i = argc - 1; i >= 0; i--) {
    *esp -= 4;
    just_in_case();
    *((void**) *esp) = argv_addr[i];
  }

  // setting **argv (addr of stack, esp)
  *esp -= 4;
  just_in_case();
  *((void**) *esp) = (*esp + 4);

  // setting argc
  *esp -= 4;
  just_in_case();
  *((int*) *esp) = argc;

  // setting ret addr
  *esp -= 4;
  just_in_case();
  *((int*) *esp) = 0;

}

static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();
  just_in_case();
  bool success = (pagedir_get_page (t->pagedir, upage) == NULL);
  success = success && pagedir_set_page (t->pagedir, upage, kpage, writable);

  return success;
}