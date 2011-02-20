#include <param.h>
#include <x86.h>
#include <proto.h>
#include <proc.h>
//
#include <page.h>
#include <vm.h>
//
#include <buf.h>
#include <conf.h>
#include <hd.h>
//
#include <super.h>
#include <inode.h>
#include <file.h>

/*
 * vm.c
 *
 *
 * */

/*
 * the proc[0]'s page directory, as initialized, it will map the top 4map virtual memory 
 * as physical memory, by the first middle page table.
 * */
struct pde pgd0[1024] __attribute__((aligned(4096)));

/* --------------------------------------------------------- */

/*
 * given the current proc's page directory, find the pte where 
 * the virtual address lied in. If the 'creat' parameter is set,
 * allocate a page as middle page table and always success.
 * */
struct pte* find_pte(uint vaddr, uint creat){
    struct pde *pde, *pgd; 
    struct pte *pt;
    struct page *pg;

    if (vaddr < KMEM_END) {
        panic("find_pte(): don't touch kernel's address space.");
    }
    pgd = cu->p_vm.vm_pgd;
    pde = &pgd[PDX(vaddr)];
    if ((pde->pd_flag & PTE_P)==0) {
        if (creat==0) {
            return NULL;
        }
        pg = pgalloc();
        pde->pd_flag = PTE_P | PTE_U | PTE_W;
        pde->pd_off = pg->pg_num;
        pt = (struct pte*)(pde->pd_off << 12);
        memset(pt, 0, PAGE);
        lpgd(pgd);
    }
    pt = (struct pte*)(pde->pd_off << 12);
    return &pt[PTX(vaddr)];
}

/* initialize the PGD as mapping the kernel's address space. 
 * called in mm_init() and vm_clone().
 * */
int pgd_init(struct pde *pgd){
    uint pn;

    // map the entire physical memory into the kernel's address space, via 4mb big pages
    for (pn=0; pn<PMEM/(PAGE*1024); pn++) {
        pgd[pn].pd_off = pn << 10;
        pgd[pn].pd_flag = PTE_PS | PTE_P | PTE_W; // note: set it 4mb via a PTE_PS
    }
    // all the rest pde are user's land.
    for (pn=PMEM/(PAGE*1024); pn<1024; pn++) {
        pgd[pn].pd_off = 0;
        pgd[pn].pd_flag = PTE_U;
    }
}

/* copy page tables */
int pgd_copy(struct pde *pgd, uint base, uint size, uint flag){
    struct pde *pde;
    struct pte *pte, *old_pt, *new_pt;
    struct page *pg;
    uint pdn, pn;

    for (pdn=PPN(base)/1024; pdn<PPN(base+size)/1024; pdn++){
        pde = &(cu->p_vm.vm_pgd[pdn]);
        if (pde->pd_flag & PTE_P) {
            old_pt = (struct pte*)(pde->pd_off << 12);
            new_pt = (struct pte*)kmalloc(PAGE);
            pgd[pdn].pd_off = PPN(new_pt);
            pgd[pdn].pd_flag = PTE_U | PTE_W | PTE_P;
            for(pn=0; pn<1024; pn++) {
                new_pt[pn].pt_off = old_pt[pn].pt_off;
                new_pt[pn].pt_flag = flag;
                old_pt[pn].pt_flag = flag; // note: old PTE is also modified.
                // increase page's ref count
                pg = pgfind(pdn*1024+pn);
                pg->pg_count++;
            }
        }
    }
    return 0;
}

/* --------------------------------------------------------- */

/*
 * Clone the kernel's address space first. 
 * */
int vm_clone(struct vm *to, struct vm *from){
    int i;

    to->vm_pgd = (struct pde*)kmalloc(PAGE);
    pgd_init(to->vm_pgd);
    for (i=PMEM/(PAGE*1024); i<1024; i++) {
        to->vm_pgd[i] = from->vm_pgd[i];
        to->vm_pgd[i].pd_flag &= ~PTE_W;
    }
    return 0;
}

/**/
int vm_free(){
}

/* Have a check of virtual memory area on getting a user space 
 * pointer, on writing a write protected page, x86 do not raise 
 * a page fault in ring0, so simulate a write only access as 
 * what mmu does if nessary.
 * note: do not verify kernel memory.
 * */
int vm_verify(uint vaddr, uint size){
    struct pde *pgd;
    struct pte *pte;
    struct page *pg;
    uint page;
    
    if (vaddr<KMEM_END || size<=0) {
        return -1;
    }
    for (page=PG_ADDR(vaddr); page<=PG_ADDR(vaddr+size-1); page+=PAGE) {
        pte = find_pte(page, 1);
        if ((pte->pt_flag & PTE_P)==0) {
            do_no_page(page);
        }
        else if ((pte->pt_flag & PTE_W)==0) {
            do_wp_page(page);
        }
    }
    return 0;
}

