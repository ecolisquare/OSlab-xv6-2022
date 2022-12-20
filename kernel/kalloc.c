// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct mem{
  struct spinlock lock;
  struct run *freelist;
};
 
struct mem kmem[NCPU];
 
void 
kfree_specific(void *pa, int cpuid)
{
  memset(pa, 1, PGSIZE);
 
  struct run* r = (struct run*)pa;
  struct mem* cpu_mem = &(kmem[cpuid]);
  acquire(&cpu_mem->lock);
  r->next = cpu_mem->freelist;
  cpu_mem->freelist = r;
  release(&cpu_mem->lock);
}

void
kinit()
{
  for(int i = 0;i < NCPU;i++){
    initlock(&kmem[i].lock,"kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  push_off();
  int hart = cpuid();
  pop_off();
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    kfree_specific(p, hart);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
 
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
 
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
 
  push_off();
  int hart = cpuid();
  pop_off();
 
  r = (struct run*)pa;
  //int hart = get_pa_cpu_id((uint64)r);
  struct mem* cpu_mem = &kmem[hart];
  acquire(&cpu_mem->lock);
  r->next = cpu_mem->freelist;
  cpu_mem->freelist = r;
  release(&cpu_mem->lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int hart = cpuid();
  pop_off();
  struct mem* cpu_mem = &kmem[hart];
  acquire(&cpu_mem->lock);
  r = cpu_mem->freelist;
  if(r){
    cpu_mem->freelist = r->next;
    release(&cpu_mem->lock);
  }
  else // "steal" free memory from other cpu mem list
  {
    release(&cpu_mem->lock);
    for (int i = 0; i < NCPU ; i++) {
      cpu_mem = &kmem[i];
      acquire(&cpu_mem->lock);
      r = cpu_mem->freelist;
      if (r){
        cpu_mem->freelist = r->next;
        release(&cpu_mem->lock);
        break;
      }
      release(&cpu_mem->lock);
    }
  }
 
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
