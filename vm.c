#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()
struct segdesc gdt[NSEGS];

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpunum()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);

  // Map cpu, and curproc
  c->gdt[SEG_KCPU] = SEG(STA_W, &c->cpu, 8, 0);

  lgdt(c->gdt, sizeof(c->gdt));
  loadgs(SEG_KCPU << 3);
  
  // Initialize cpu-local storage.
  cpu = c;
  proc = 0;
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)p2v(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table 
    // entries, if necessary.
    *pde = v2p(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;
  
  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
// 
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP, 
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (p2v(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start, 
                (uint)k->phys_start, k->perm) < 0)
      return 0;
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(v2p(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  pushcli();
  cpu->gdt[SEG_TSS] = SEG16(STS_T32A, &cpu->ts, sizeof(cpu->ts)-1, 0);
  cpu->gdt[SEG_TSS].s = 0;
  cpu->ts.ss0 = SEG_KDATA << 3;
  cpu->ts.esp0 = (uint)proc->kstack + KSTACKSIZE;
  ltr(SEG_TSS << 3);
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");
  lcr3(v2p(p->pgdir));  // switch to new address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;
  
  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, v2p(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, p2v(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}
#define INFINITY 0xffffffff;
#ifndef SEL_NONE
#define END proc->pg_data.pgs + MAX_TOTAL_PAGES
#ifdef SEL_FIFO
struct pg* pick_page(void) {
  struct pg* it = proc->pg_data.pgs; 
  struct pg dummy_min;
  struct pg* min = &dummy_min;
  dummy_min.ctime = INFINITY; 
  while(it != END) {
    if(it->state == RAM)
      min = min->ctime > it->ctime ? it : min;
    ++it; 
  }
  if (&dummy_min == min)
    panic("could not choose page");
  return min;
}
#endif

#ifdef SEL_SCFIFO
struct pg* pick_page(void) {
  struct pg* it = proc->pg_data.pgs; 
  struct pg dummy_min;
  struct pg* min = &dummy_min;
  pde_t* pe;
  
  dummy_min.ctime = INFINITY;
  for(;;) {
    it = proc->pg_data.pgs + 3;
    while(it != END) {
      if(it->state == RAM)
        min = min->ctime > it->ctime ? it : min;
      ++it; 
    }
    if(min == &dummy_min)
      panic("could not choose page");

    // check the PTE_A thing
    pe = walkpgdir(proc->pgdir, (void*) min->id, 0);
    if (*pe & PTE_A) // accesses
      *pe &= (~PTE_A); // turn it off
    else
      break; // found a fifo..
  }
  return min;
}
#endif

#ifdef SEL_NFU
struct pg* pick_page(void) {
  struct pg* it = proc->pg_data.pgs; 
  struct pg dummy_min;
  struct pg* min = &dummy_min;
  dummy_min.ctime = INFINITY; 
  while(it != END) {
    if(it->state == RAM)
      min = min->nfu_time > it->nfu_time ? it : min;
    ++it; 
  }
  if (&dummy_min == min)
    panic("could not choose page");
  return min;
}

void nfu_update(void) {
  struct pg* iter = proc->pg_data.pgs;
  pde_t* pte;

  while(iter < END) {
    if(iter->state != PG_UNUSED) {
      iter->nfu_time >>= 1; // ageing -  shift it
      pte = walkpgdir(proc->pgdir, (void*) iter->id, 0);
      
      if(*pte && *pte & PTE_A) { // was used so turn the bit one
        iter->nfu_time |= 0x8000;
        *pte = *pte & ~PTE_A;
      }
    }
      ++iter;
  }
}
#endif
/*struct pg* 
pick_page(void) {
  struct pg* it = proc->pg_data.pgs+3;
  while(it != proc->pg_data.pgs + MAX_TOTAL_PAGES) {
    if (it->state == RAM)
      break;
    ++it;
  }
  return it;
}*/
#endif
int get_swap_cell(struct pg* pg) {
  int i = 0;
  struct swapfile_cell* cells = proc->pg_data.cells;
  while (i < MAX_TOTAL_PAGES) {                                                    
    if (!cells[i].taken) {                                                         
      cells[i].index = i;                                                          
      cells[i].taken = 1;                                                          
      pg->idx_swp = i;                                                              
      return i;                                                           
    }                                                                              
    ++i;                                                                           
  }                                                                                
  panic("no cells");
  return -1; 
}
#ifndef SEL_NONE
static int 
swapout(struct pg* pg) {
  pde_t* pe;   // page table entry of the page we want to swapout
  uint va = pg->id;     // id of the page
  ++proc->pg_data.pg_swapouts;
  if(writeToSwapFile(proc, (char *) va, get_swap_cell(pg) * PGSIZE, PGSIZE) < 0)
    panic("write to swap file faieled wdasdad ");
  if((pe = walkpgdir(proc->pgdir,(void *)  va, 0)) == 0)
    panic("swapout, address should exist \n");
  pg->state = DISK; 
  *pe = (*pe | PTE_PG) & ~PTE_P;
  kfree((char *) p2v(PTE_ADDR(*pe)));
  return 1;
}
#endif

#ifndef SEL_NONE
static struct pg*
find_pg(uint va) {
  struct pg* pg = proc->pg_data.pgs;
  uint id = va & 0xFFFFF000; 
  while(pg < proc->pg_data.pgs + MAX_TOTAL_PAGES) {
    if (pg->id == id)
      return pg;
    ++pg;
  }
  return 0;
}

static void
add_pg_to_metadata(uint va) {
  struct pg* p_iter = proc->pg_data.pgs;
  while(p_iter->state != PG_UNUSED &&
      p_iter < proc->pg_data.pgs + MAX_TOTAL_PAGES)
    p_iter++;
  p_iter->state = RAM;
  p_iter->id = (uint) PTE_ADDR(va);
  p_iter->idx_swp = -1;
  p_iter->ctime = ticks;
}
// this page has to be in the ram ? amiright?
static void
rm_pg_metadata(char* va){
  struct pg* pg = proc->pg_data.pgs;
  pg = find_pg((uint)va);
  if (pg && 0) {
   switch (pg->state) {
     case PG_UNUSED:
       break;
     case DISK:
       proc->pg_data.cells[pg->idx_swp].taken = 0;
     case RAM:
       pg->state = PG_UNUSED;
   } 
  }
}
#endif
// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
  int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;
  

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
#ifndef SEL_NONE
    ++proc->pg_data.total_pgs;
    if (proc->pg_data.ram_pgs >= MAX_PSYC_PAGES) { // need to swap one out
      swapout(pick_page());
    } else ++proc->pg_data.ram_pgs;
#endif
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    mappages(pgdir, (char*)a, PGSIZE, v2p(mem), PTE_W|PTE_U);
 #ifndef SEL_NONE
    add_pg_to_metadata(a);
 #endif
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a += (NPTENTRIES - 1) * PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0) {
        panic("kfree");
      }
      if (!(*pte & PTE_PG)) {
        char *v = p2v(pa);
        kfree(v);
      }
      *pte = 0;
#ifndef SEL_NONE
      // if this is aprocess freeing its own memory.
      if (proc != 0 && pgdir == proc->pgdir) {
        rm_pg_metadata((char *) a);
      }
      // if it's a parent freeing his zombie kiddie we need do something else.
      if(proc != 0 && pgdir != proc->pgdir) {
        // An option is to ignore it. TODO(bilals) lambda
      }
#endif
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = p2v(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte, *pg_pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P) && !(*pte & PTE_PG))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
#ifndef SEL_NONE
    if (*pte & PTE_PG) { // the page is out out.
        if(mappages(d, (void*)i, PGSIZE, 0x0, flags) < 0)
          goto bad;
        // So we've mapped an entry for le page but! alas for this page is well
        // paged out. So we must pewpew its present flag!. praise be to the lord
        pg_pte = walkpgdir(d, (void*)i, 0); // yikes.
        *pg_pte &= ~PTE_P;
        continue;
    }
#endif
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)p2v(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, v2p(mem), flags) < 0)
      goto bad;
    pg_pte = walkpgdir(d, (void*)i, 0); // yikes.
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)p2v(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}
#ifndef SEL_NONE
static int
swapin(uint va) { //  fault address
  char* mem;
  struct pg* pg = find_pg(va);
  if(!pg)
    panic("not found");
  va = PTE_ADDR(va);
  mem = kalloc(); 
  if(mem == 0)
    panic("kalloc failed in swapin");
  memset(mem, 0, PGSIZE);
  readFromSwapFile(proc, mem, pg->idx_swp * PGSIZE, PGSIZE);
  mappages(proc->pgdir, (void*) va, PGSIZE, (uint)v2p(mem), PTE_U | PTE_W);
  
  pg->state = RAM;
  proc->pg_data.cells[pg->idx_swp].taken = 0;
  pg->idx_swp = -1;
  pg->ctime = ticks;


  return 1;
}
int
pg_fault(void) {
  uint va;
  pde_t* pte;
  struct pg* pg;
  char c[4];
  va = rcr2();
  pg = find_pg(PTE_ADDR(va));
  if(va == 0x4000){
   readFromSwapFile(proc, c, pg->idx_swp * PGSIZE, sizeof(c));
   cprintf("pid: %d <%d %d %d %d> \n", proc->pid, c[0], c[1], c[2], c[3]);
  }
  pte = walkpgdir(proc->pgdir, (void *) va, 0);

  if(!pte || !(*pte & PTE_PG)) {
    d2;
    return 0;
  }
  ++proc->pg_data.pg_faults;
  if (proc->pg_data.ram_pgs >= MAX_PSYC_PAGES) {
    swapout(pick_page());
  }
  return swapin(va);
}
#endif
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

