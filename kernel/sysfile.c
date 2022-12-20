//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

struct VMA*
get_unused_vma()
{
  struct proc* p = myproc();
  for (int i = 0; i < 16; i++) {
    if (p->vma[i].valid == 0) {
      return &(p->vma[i]);
    }
  }
  return 0;
}

struct VMA*
get_vma_by_address(uint64 va)
{
  struct proc* p = myproc();
  for (int i = 0; i < 16; i++) {
    struct VMA* curr = &(p->vma[i]);
    if (curr->valid == 0)
      continue;
    // the address is covered by the vma
    if (curr->addr <= va && curr->addr + curr->length > va) {
      return curr;
    }
  }
  return 0;
}

// find available virtual address at the top of process
// and then increase the process size
// it is something like sbrk
uint64
get_vma_start_addr(uint64 length)
{
  struct proc* p = myproc();
  uint64 addr = PGROUNDUP(p->sz);
  p->sz = addr + length;
  return addr;
}


// void *mmap(void *addr, uint64 length, int prot, int flags, int fd, uint64 offset);
// prot: read/write/read_and_write
// flags: map_shared/map_private, flags will be either MAP_SHARED,
// meaning that modifications to the mapped memory should be written back to
// the file, or MAP_PRIVATE, meaning that they should not.
uint64
sys_mmap()
{
  uint64 addr = 0, length = 0, offset = 0;
  int prot = 0, flags = 0;
  struct file *f;
  argaddr(0, &addr);
  argaddr(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argfd(4, 0, &f);
  argaddr(5, &offset);

  // check operation conflict
  if ((prot & PROT_WRITE) && !(f->writable) && !(flags & MAP_PRIVATE))
    return -1;
  if ((prot & PROT_READ) && !(f->readable))
    return -1;

  struct VMA* vma = get_unused_vma();
  if (0 == vma) // not enough vma
    return -1;

  // add file reference
  filedup(f);

  // init the vma
  vma->addr = get_vma_start_addr(length);
  vma->length = length;
  vma->f = f;
  vma->flags = flags;
  vma->prot = prot;
  vma->offset = offset;
  vma->valid = 1;

  int page_num = length / PGSIZE + ((length % PGSIZE == 0) ? 0 : 1);
  int mask = 1;
  for (int i = 0; i < page_num; i++) {
    vma->bitmap |= mask;
    mask = mask << 1;
  }
  return vma->addr;
}

uint64
clean_vma(struct VMA* vma, uint64 addr, uint64 length)
{
  int page_number = length / PGSIZE + ((length % PGSIZE == 0) ? 0 : 1);
  int start = (addr - vma->addr) / PGSIZE;
  for (int i = start; i < page_number; i++) {
    vma->bitmap = vma->bitmap & (~(1 << i));
  }
  if (0 == vma->bitmap) {
    fileclose(vma->f);
    memset((void*)vma, 0, sizeof (struct VMA));
  }
  return 0;
}

uint64
unmap_vma(uint64 addr, uint64 length)
{
  struct VMA* vma = get_vma_by_address(addr);
  if (0 == vma)
    return -1;

  int total = sizeof (uint64);
  struct proc* proc = myproc();

  uint64 bitmap = vma->bitmap;
  for (int i = 0; i < total; i++) {
    if ((bitmap & 1) != 0) {
      uint64 addr = vma->addr + PGSIZE * i;
      uvmunmap(proc->pagetable, addr, 1, 0);
      bitmap = (bitmap >> 1);
    }
  }
  return 0;
}

uint64
flush_content(struct VMA* vma, uint64 addr, uint64 length)
{
  struct proc* proc = myproc();
  int page_number = length / PGSIZE + ((length % PGSIZE == 0) ? 0 : 1);
  int start = (addr - vma->addr) / PGSIZE;
  for (int i = start; i < page_number; i++) {
//    if (((vma->bitmap >> i) & 1) == 1) {
      vma->f->off = PGSIZE * i;
      // write in memory content to file
      filewrite(vma->f, vma->addr, PGSIZE);
//      uvmunmap(proc->pagetable, addr + i * PGSIZE, 1, 1);
//    }
  }
  uvmunmap(proc->pagetable, addr, page_number, 1);
  return 0;
}

uint64
sys_munmap_helper(uint64 addr, uint64 length)
{
  // get the address's vma
  struct VMA* vma = get_vma_by_address(addr);
  if (0 == vma)
    return -1;

  // no need to write back
  if (vma->flags & MAP_PRIVATE) {
    return clean_vma(vma, addr, length);
  }
  // we need to write in-memory content to file
  uint64 ret = flush_content(vma, addr, length);
  if (ret < 0)
    return ret;

  return clean_vma(vma, addr, length);
}


uint64
sys_munmap()
{
  uint64 addr = 0, length = 0;
  argaddr(0, &addr);
  argaddr(1, &length);

  return sys_munmap_helper(addr, length);
}


uint64
handle_mmap_page_fault(uint64 scause, uint64 va)
{
  struct VMA* vma = get_vma_by_address(va);
  if (0 == vma)
    return -1;

  // write
  if (scause == 15 && (!(vma->prot & PROT_WRITE)))
    return -1;
  if (scause == 13 && (!(vma->prot & PROT_READ)))
    return -1;

  // allocate new physical address
  void* pa = kalloc();
  if (0 == kalloc())
    return -1;
  memset(pa, 0, PGSIZE);

  struct proc* p = myproc();
  // set pages permission
  int flag = 0;
  if (vma->prot & PROT_WRITE)
    flag |= PTE_W;
  if (vma->prot & PROT_READ)
    flag |= PTE_R;
  flag |= PTE_U;
  flag |= PTE_X;

  // page align
  va = PGROUNDDOWN(va);
  // map the virtual address and physical address
  if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, flag) < 0) {
    kfree(pa);
    return -1;
  }

  // read file content to memory
  ilock(vma->f->ip);
  readi(vma->f->ip, 0, (uint64)pa, vma->offset + va - vma->addr, PGSIZE);
  iunlock(vma->f->ip);
  return 0;
}