/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: memory management unit (MMU)
 * implementation of 2 translation mechanisms: page table and software TLB
 */

#include "egos.h"
#include "disk.h"
#include <string.h>

/* Interface of the paging device, see earth/dev_page.c */
void paging_init();
int paging_invalidate_cache(int frame_id);
int paging_write(int frame_id, int page_no);
char* paging_read(int frame_id, int alloc_only);

/* Allocation and free of physical frames */
#define NFRAMES 256
struct frame_mapping {
    int use;     /* Is the frame allocated? */
    int pid;     /* Which process owns the frame? */
    int page_no; /* Which virtual page is the frame mapped to? */
} table[NFRAMES];
unsigned int* pid_to_pagetable_base[MAX_NPROCESS];

int mmu_alloc(int* frame_id, void** cached_addr) {
    for (int i = 0; i < NFRAMES; i++)
        if (!table[i].use) {
            *frame_id = i;
            *cached_addr = paging_read(i, 1);
            table[i].use = 1;
            return 0;
        }
    FATAL("mmu_alloc: no more available frames");
}

int mmu_free(int pid) {
    for (int i = 0; i < NFRAMES; i++)
        if (table[i].use && table[i].pid == pid) {
            paging_invalidate_cache(i);
            memset(&table[i], 0, sizeof(struct frame_mapping));
        }
    pid_to_pagetable_base[pid] = 0;
}

/* Software TLB Translation */
int soft_mmu_map(int pid, int page_no, int frame_id) {
    table[frame_id].pid = pid;
    table[frame_id].page_no = page_no;
}

int soft_mmu_switch(int pid) {
    static int curr_vm_pid = -1;
    if (pid == curr_vm_pid) return 0;

    /* Unmap curr_vm_pid from the user address space */
    for (int i = 0; i < NFRAMES; i++)
        if (table[i].use && table[i].pid == curr_vm_pid)
            paging_write(i, table[i].page_no);

    /* Map pid to the user address space */
    for (int i = 0; i < NFRAMES; i++)
        if (table[i].use && table[i].pid == pid)
            memcpy((void*)(table[i].page_no << 12), paging_read(i, 0), PAGE_SIZE);

    curr_vm_pid = pid;
}

/* Page Table Translation
 *
 * The code below creates an identity mapping using RISC-V Sv32.
 *
 * mmu_map() and mmu_switch() using page tables is given to students
 * as a course project. After this project, every process should have
 * its own set of page tables. mmu_map() will modify entries in these
 * tables and mmu_switch() will modify satp (page table base register)
 */

#define FLAG_VALID_RWX 0xF
#define FLAG_NEXT_LEVEL 0x1
static unsigned int frame_id, *root, *leaf;

void setup_identity_region(unsigned int addr, int npages) {
    /* Allocate the leaf page table */
    earth->mmu_alloc(&frame_id, (void**)&leaf);
    memset(leaf, 0, PAGE_SIZE);

    /* Setup the entry in the root page table */
    int vpn1 = addr >> 22;
    root[vpn1] = ((unsigned int)leaf >> 2) | FLAG_NEXT_LEVEL;

    /* Setup the entries in the leaf page table */
    int vpn0 = (addr >> 12) & 0x3FF;
    for (int i = 0; i < npages; i++)
        leaf[vpn0 + i] = ((addr + i * PAGE_SIZE) >> 2) | FLAG_VALID_RWX;
}

void pagetable_identity_mapping(int pid) {
    /* Allocate the root page table and set the page table base (satp) */
    earth->mmu_alloc(&frame_id, (void**)&root);
    memset(root, 0, PAGE_SIZE);

    pid_to_pagetable_base[pid] = root;
    /* Allocate the leaf page tables */
    setup_identity_region(0x02000000, 16);   /* CLINT */
    setup_identity_region(0x10013000, 1);    /* UART0 */
    setup_identity_region(0x20400000, 1024); /* boot ROM */
    setup_identity_region(0x20800000, 1024); /* disk image */
    setup_identity_region(0x08000000, 8);    /* ITIM memory */
    setup_identity_region(0x80000000, 1024); /* DTIM memory */

    /* Translation will start when the earth main() invokes mret so that the processor enters supervisor mode from machine mode */
}

int pagetable_mmu_map(int pid, int page_no, int frame_id) {
    /* Student's code goes here: */
    FATAL("mmu_map() using page table translation not implemented");
    /* Student's code ends here. */
}

int pagetable_mmu_switch(int pid) {
    /* Student's code goes here: */
    FATAL("mmu_switch() using page table translation not implemented");
    /* Student's code ends here. */
}

/* MMU Initialization */
void platform_detect(int id) {
    earth->platform = ARTY;
    /* Skip the illegal store instruction */
    int mepc;
    asm("csrr %0, mepc" : "=r"(mepc));
    asm("csrw mepc, %0" ::"r"(mepc + 4));
}

void mmu_init() {
    /* Choose memory translation mechanism */
    CRITICAL("Choose a memory translation mechanism:");
    printf("  Enter 0: page tables  (QEMU)\r\n");
    printf("  Enter 1: software TLB (QEMU or Arty board)\r\n");

    char buf[2];
    for (buf[0] = 0; buf[0] != '0' && buf[0] != '1'; earth->tty_read(buf, 2));
    INFO("%s translation is chosen", (buf[0] == '0')? "Page table" : "Software");

    /* Check whether the hardware platform supports supervisor mode */
    earth->platform = QEMU;
    earth->excp_register(platform_detect);
    /* This memory access triggers an exception on Arty, but not QEMU */
    *(int*)(0x1000) = 1;
    earth->excp_register(NULL);
    if (earth->platform == ARTY && buf[0] == '0')
        FATAL("Arty board doesn't support page tables (supervisor mode).");

    /* Initialize MMU interface functions */
    earth->mmu_free = mmu_free;
    earth->mmu_alloc = mmu_alloc;
    earth->mmu_map = soft_mmu_map;
    earth->mmu_switch = soft_mmu_switch;

    if (buf[0] == '0') {
        pagetable_identity_mapping(0);
        earth->mmu_map = pagetable_mmu_map;
        earth->mmu_switch = pagetable_mmu_switch;
    }

    /* Initialize the paging device */
    paging_init();
}
