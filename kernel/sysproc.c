#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// sys_flip_display: zero-copy page flip.
//
// Syscall argument 0: user virtual address of a page-aligned buffer
// that is exactly GPU_FB_PAGES (300) * PGSIZE bytes (i.e. 640x480x4 =
// 1,228,800 bytes).  The buffer must already be fully mapped in the
// calling process's address space.
//
// Walks the caller's page table, translates each user VA to its
// physical page, and re-points the GPU resource backing to those
// physical pages.  Returns 0 on success, -1 on error.
uint64
sys_flip_display(void)
{
  uint64 ubuf;
  argaddr(0, &ubuf);

  if(ubuf == 0 || (ubuf % PGSIZE) != 0)
    return -1;

  // Refuse if the buffer would extend beyond the user-accessible VA range.
  if(ubuf + (uint64)GPU_FB_PAGES * PGSIZE > MAXVA)
    return -1;

  // One page (4096 B) easily holds 300 uint64 physical addresses.
  uint64 *pas = (uint64*)kalloc();
  if(pas == 0)
    return -1;

  struct proc *p = myproc();
  for(int i = 0; i < GPU_FB_PAGES; i++){
    uint64 va = ubuf + (uint64)i * PGSIZE;
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0
                || (*pte & PTE_R) == 0 || (*pte & PTE_W) == 0){
      kfree(pas);
      return -1;
    }
    pas[i] = PTE2PA(*pte);
  }

  int rc = virtio_gpu_flip(pas, GPU_FB_PAGES);
  kfree(pas);
  if(rc < 0)
    return -1;
  return 0;
}

// sys_map_display: map the GPU's kernel framebuffer pages (fb[]) directly
// into the calling process's address space with PTE_U|PTE_R|PTE_W.
//
// Syscall argument 0: desired user virtual address (must be page-aligned).
//   Pass 0 to let the kernel auto-select the next available VA above p->sz.
//
// Returns the mapped virtual address on success, (uint64)-1 on failure.
//
// CRITICAL: the framebuffer pages are kernel-owned (allocated once in
// virtio_gpu_init).  We install PTEs that point at them but the
// teardown path (uvmfree -> uvmunmap_leaves_nofree) is responsible for
// clearing those PTEs without kfree-ing the underlying pages.
uint64
sys_map_display(void)
{
  uint64 addr;
  argaddr(0, &addr);

  struct proc *p = myproc();
  uint64 size = (uint64)GPU_FB_PAGES * PGSIZE;
  uint64 va;

  if(addr == 0){
    // Auto-pick a page-aligned VA strictly above p->sz that fits below
    // the trapframe.  PGROUNDUP(0) is 0, so guarantee at least one page.
    va = PGROUNDUP(p->sz);
    if(va == 0)
      va = PGSIZE;
  } else {
    if((addr % PGSIZE) != 0)
      return -1;
    va = addr;
  }

  // Range check: must lie within user VA space (below the trapframe).
  if(va == 0 || va + size > TRAPFRAME)
    return -1;

  // Collision check: every target page must currently be unmapped.
  for(uint64 a = va; a < va + size; a += PGSIZE){
    pte_t *pte = walk(p->pagetable, a, 0);
    if(pte != 0 && (*pte & PTE_V))
      return -1;
  }

  // Resolve the framebuffer's physical pages into a kalloc'd scratch
  // page (4096 B holds 512 uint64, ample for 300 entries).
  uint64 *fb_pa = (uint64*)kalloc();
  if(fb_pa == 0)
    return -1;
  virtio_gpu_get_fb_pages(fb_pa);

  // Install one PTE per page.  On failure roll back any PTEs we already
  // installed so we don't leave a partial mapping behind.
  for(int i = 0; i < GPU_FB_PAGES; i++){
    uint64 a = va + (uint64)i * PGSIZE;
    if(mappages(p->pagetable, a, PGSIZE, fb_pa[i],
                PTE_R | PTE_W | PTE_U) != 0){
      if(i > 0)
        uvmunmap(p->pagetable, va, i, 0);
      kfree(fb_pa);
      return -1;
    }
  }
  kfree(fb_pa);

  return va;
}
