#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <stdlib.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "devices/block.h"

// ---Solutie---

const int MIN_FILENAME = 1;
const int MAX_FILENAME = 14;

typedef int pid_t;

struct file_descriptor
{
  int fd;
  struct file *file;
  struct list_elem elem;

};

static struct lock filesys_lock; 
// functie ajutatoare
bool is_valid_ptr(const void *ptr);
bool is_valid_filename(const void *file);

static void syscall_handler (struct intr_frame *);

static void halt(void);

static pid_t exec(const char *cmd_line);
static int wait(pid_t pid);

static bool create(const char *file, unsigned initial_size);
static bool remove(const char *file);

static int open(const char *file);
static void close(int fd);

static filesize(int fd);
static int read(int fd, void *buffer, unsigned size);
static int write(int fd, const void *buffer, unsigned size);

static void seek(int fd, unsigned position);
static unsigned tell(int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  // printf("%04x\n", f->cs);
  uint32_t *esp = f->esp;
  uint32_t *argv0 = esp + 1;
  uint32_t *argv1 = esp + 2;
  uint32_t *argv2 = esp + 3;

  if (!is_valid_ptr(esp) || !is_valid_ptr(argv0) 
    || !is_valid_ptr(argv1) || !is_valid_ptr(argv2)) 
  {
    exit(-1);
  }

  uint32_t syscall_num = *esp;
  // printf("System call : %d\n", syscall_num);
  // printf("System call : %x\n", esp);
  // printf("argv number : %d\n", *(esp + 5));
  // hex_dump(esp, esp, 64, true);
  switch (syscall_num) 
  {
  	case SYS_HALT:
      halt();
  		break;
  	case SYS_EXIT:
      exit(*argv0);
  		break;
  	case SYS_EXEC:
      f->eax = exec((char *)*argv0);
  		break;
  	case SYS_WAIT:
      f->eax = wait(*argv0);
  		break;
  	case SYS_CREATE:
      f->eax = create((char *)*argv0, *argv1);
  		break;
  	case SYS_REMOVE:
      f->eax = remove((char *)*argv0);
  		break;
  	case SYS_OPEN:
      f->eax = open((char *)*argv0);
  		break;
  	case SYS_FILESIZE:
      f->eax = filesize(*argv0);
  		break;
  	case SYS_READ:
      f->eax = read(*argv0, (void *)*argv1, *argv2);
  		break;
  	case SYS_WRITE:
  		f->eax = write(*argv0, (void *)*argv1, *argv2);
  		break;
  	case SYS_SEEK:
      seek(*argv0, *argv1);
  		break;
  	case SYS_TELL:
      f->eax = tell(*argv0);
  		break;
  	case SYS_CLOSE:
      close(*argv0);
  		break; 
  	default:
  		break; 		  	
  }
  // thread_exit ();
  // hex_dump(f->eip, f->eip, 64, true);
}

/* Verifica oricare *ptr sa fie valid --
   1. ptr nu trebuie sa fie un pointer null;
   2. ptr trebuie sa pointeze spre memoria userului;
   3. ptr nu trebuie sa pointeze spre memoria virtuala nemapata.*/
bool 
is_valid_ptr(const void *ptr) 
{
  if (ptr == NULL 
    || !is_user_vaddr(ptr) 
    || pagedir_get_page(thread_current()->pagedir, ptr) == NULL)
    return false;

  return true;
}

/* Verifica fiecare *file sa fie un valid filename. */
bool 
is_valid_filename(const void *file)
{
  if (!is_valid_ptr(file)) 
    exit(-1);

  int len = strlen(file);
  return len >= MIN_FILENAME && len <= MAX_FILENAME;
}

struct file_descriptor *
get_openfile(int fd)
{
  struct list *list = &thread_current()->open_fd;
  for (struct list_elem *e = list_begin (list); 
                          e != list_end (list); 
                          e = list_next (e))
  {
    struct file_descriptor *f = 
        list_entry(e, struct file_descriptor, elem);
    if (f->fd == fd)
      return f;
    else if (f->fd > fd)
      return NULL;
  }
  return NULL;
}
void 
close_openfile(int fd)
{
  struct list *list = &thread_current()->open_fd;
  for (struct list_elem *e = list_begin (list); 
                          e != list_end (list); 
                          e = list_next (e))
  {
    struct file_descriptor *f = 
        list_entry(e, struct file_descriptor, elem);
    if (f->fd == fd)
    { 
      list_remove(e);
      file_close(f->file);
      free(f);
      return;
    }
    else if (f->fd > fd)
      return ;
  }
  return ;
}

/* Inchide Pintos. */
static void 
halt(void) 
{
  shutdown_power_off();
}

/* Inchide programul curent al userului.

   Returneaza statusul la kernel.
   status = 0 -- success
   status = nonzero -- error */
void 
exit(int status)
{
  struct thread *cur = thread_current();

  printf("%s: exit(%d)\n", cur->name, status);

    /* Daca parintele lui inca asteapta dupa acesta,
     transmite parintelui statusul de iesire. */
  if (cur->parent != NULL)
  {
    cur->parent->child_exit_status = status;
    // printf("parent %s: child_exit(%d)\n", cur->parent->name, cur->parent->child_exit_status);
  }

  /* Inchide toate fisierele deschise. */
  // mmb -- the key to multi-oom
  while (!list_empty(&cur->open_fd)) 
  {
    close(list_entry(list_begin(&cur->open_fd), struct file_descriptor, elem)->fd);  
  }

  /* Inchide fisierul de executie. */
  file_close(cur->file);
  // ---Solutie---

  thread_exit();
}

/* Ruleaza executabilui a carui nume este dat in command line,
   trecand orice argumente date.

   Returneaza id-ul programului de proces (pid).
   Trebuie sa returneze pid -1, care nu trebuie sa fie un pid valid,
   daca programul nu poate sa se incarce sau sa ruleze.*/
static pid_t 
exec(const char *cmd_line)
{  


  if (!is_valid_ptr(cmd_line))
    exit(-1);


  lock_acquire(&filesys_lock);
  tid_t tid = process_execute(cmd_line);
  lock_release(&filesys_lock);
  
  return tid;
}

/* Asteapta pentru un pid al unui proces fiu.
   Daca pid este in viata, asteapta pana se sfarseste.
   
   Returneaza statusul de iesire al fiului.*/
static int 
wait(pid_t pid)
{
  return process_wait(pid);
}

/* Creaza un nou fisier numit *file care are o dimensiune initiala.
   Returneaza true daca reuseste, false in caz contrar. */
static bool 
create(const char *file, unsigned initial_size)
{
  if (!is_valid_filename(file))
    return false;

  lock_acquire(&filesys_lock);
  // bool status = filesys_create(file, initial_size);
  // status goes wrong !!! I don't know why ... 

  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, file, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  lock_release(&filesys_lock);

  return success;
  // return status;
}

/* Sterge fisierul numit *file.
   Returneaza true daca reuseste, false in caz contrar. */
static bool 
remove(const char *file)
{
  if (!is_valid_filename(file))
    return false;

  bool status;

  lock_acquire(&filesys_lock);
  status = filesys_remove(file);
  lock_release(&filesys_lock);

  return status;
}

/* Asignam un fd (file descriptor) unic unui fisier.
   Returnam fd.
   This function needs modification -- Overflow 
   -- new fd is max(fd in open_fd list) + 1.
   What if open a large number of files 
   and some low-value fd is closed ? */
int 
assign_fd() 
{
  struct list *list = &thread_current()->open_fd;
  if (list_empty(list)) 
    return 2; 
  else
  {
    struct file_descriptor *f = 
        list_entry(list_back(list), struct file_descriptor, elem);
    // printf("fd : %d\n", f->fd);
        // assume there are sufficient fd space
    return f->fd + 1;
  }
}

/* Compare fd values as list_elem.
   Return true if fd(a) < fd(b), otherwise false. */
bool 
cmp_fd(const struct list_elem *a, const struct list_elem *b, void *aux)
{
  struct file_descriptor *left = list_entry(a, struct file_descriptor, elem);
  struct file_descriptor *right = list_entry(b, struct file_descriptor, elem);
  return left->fd < right->fd;
}

/* Open the file called *file, assign the opened file a fd 
   and the current process should keep track of it in open_fd list.

   Return fd if the file can be opend, otherwise -1.*/
static int 
open(const char *file)
{
  // printf("hahaha\n");
  int fd = -1;

  if (!is_valid_filename(file))
    return fd;

  lock_acquire(&filesys_lock);
  struct list *list = &thread_current()->open_fd;
  struct file *file_struct = filesys_open(file);
  if (file_struct != NULL) 
  {
    struct file_descriptor *tmp = malloc(sizeof(struct file_descriptor));    
    tmp->fd = assign_fd();
    tmp->file = file_struct;
    // tmp->tid = thread_current()->tid;
    fd = tmp->fd;
    list_insert_ordered(list, &tmp->elem, (list_less_func *)cmp_fd, NULL);
  }
  lock_release(&filesys_lock);

  // printf("open %d\n", fd);
  return fd;
}

/* Close file of fd. */
static void 
close(int fd)
{
  lock_acquire(&filesys_lock);
  close_openfile(fd);
  lock_release(&filesys_lock);

}

/* Get the size of fd file.
   Return its size. */
static int
filesize(int fd)
{
  int size = -1;

  lock_acquire(&filesys_lock);

  struct file_descriptor *file_descriptor = get_openfile(fd);
    if (file_descriptor != NULL)
      size = file_length(file_descriptor->file);

  lock_release(&filesys_lock);
  
  return size;
}

/* Read size bytes from fd into buffer.
   Return the number of bytes actully read. 
   Fd 0 -- read from the keyboard. */
static int 
read(int fd, void *buffer, unsigned size)
{
  // printf("reading\n");
  int status = -1;

  if (!is_valid_ptr(buffer) || !is_valid_ptr(buffer + size - 1)) 
    exit(-1);

  lock_acquire(&filesys_lock);
  if (fd == STDIN_FILENO) /* Fead from the keyboard.*/
  {
    uint8_t *p = buffer;
    uint8_t c;
    unsigned counter = 0;
    while (counter < size && (c = input_getc()) != 0)
    {
      *p = c;
      p++;
      counter++;
    }
    *p = 0;
    status = size - counter;

  } else if (fd != STDOUT_FILENO)
  { 
    struct file_descriptor *file_descriptor = get_openfile(fd);
    if (file_descriptor != NULL)
      status = file_read(file_descriptor->file, buffer, size);
  }

  lock_release(&filesys_lock);

  // printf("read %d\n", fd);
  return status;
}

/* Write size bytes from buffer to fd.
   Return the number of bytes actually written.
   Fd 1 -- write to the console*/
static int
write(int fd, const void *buffer, unsigned size) 
{
  int status = 0;

  if (buffer == NULL || !is_valid_ptr(buffer) || !is_valid_ptr(buffer + size - 1)) 
    exit(-1);

  lock_acquire(&filesys_lock);
	if (fd == STDOUT_FILENO) /* Write to the console.*/
	{
		putbuf(buffer, size);
		// hex_dump(buffer, buffer, 64, true);
		status = size;
	} else if (fd != STDIN_FILENO) 
  {
    struct file_descriptor *file_descriptor = get_openfile(fd);
    // printf("file %d\n", file_descriptor->file->deny_write);
    if (file_descriptor != NULL)
      status = file_write(file_descriptor->file, buffer, size);
  }

  lock_release(&filesys_lock);

  // printf("write %d %d\n", fd, status);
  return status;
}

/* Change the next byte to be read/written in open fd to position,
   expressed in bytes from the beginning of the file. */
static void 
seek(int fd, unsigned position)
{
  lock_acquire(&filesys_lock);
  struct file_descriptor *file_descriptor = get_openfile(fd);
    if (file_descriptor != NULL)
      file_seek(file_descriptor->file, position);
  lock_release(&filesys_lock);

  return ;
}

/* Get the position of the next byte te be read/writen in open fd,
   expressed in bytes from the beginning of the file.*/
static unsigned 
tell(int fd)
{
  int status = -1;

  lock_acquire(&filesys_lock);

  struct file_descriptor *file_descriptor = get_openfile(fd);
    if (file_descriptor != NULL)
      status = file_tell(file_descriptor->file);

  lock_release(&filesys_lock);

  return status;
}
