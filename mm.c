/*
 * mm.c - Memory Management: Paging & segment memory management
 */

#include <types.h>
#include <mm.h>
#include <segment.h>
#include <hardware.h>
#include <sched.h>

Byte phys_mem[TOTAL_PAGES];

/* SEGMENTATION */
/* Memory segements description table */
Descriptor  *gdt = (Descriptor *) GDT_START;
/* Register pointing to the memory segments table */
Register    gdtR;

/* PAGING */
/* Variables containing the page directory and the page table */
  
page_table_entry dir_pages[NR_TASKS][TOTAL_PAGES]
  __attribute__((__section__(".data.task")));

page_table_entry pagusr_table[NR_TASKS][TOTAL_PAGES]
  __attribute__((__section__(".data.task")));

/*Contador dels punters a un mateix directori de memoria. */
Byte assigned_dir[NR_TASKS];

unsigned int program_breaks[NR_TASKS];

/*Contador dels punters a un mateix program break*/
Byte pb_counter[NR_TASKS];

/* TSS */
TSS         tss; 



/***********************************************/
/************** PAGING MANAGEMENT **************/
/***********************************************/

/* Init page table directory */
  
void init_dir_pages()
{
int i;

for (i = 0; i< NR_TASKS; i++) {
  dir_pages[i][ENTRY_DIR_PAGES].entry = 0;
  dir_pages[i][ENTRY_DIR_PAGES].bits.pbase_addr = (((unsigned int)&pagusr_table[i]) >> 12);
  dir_pages[i][ENTRY_DIR_PAGES].bits.user = 1;
  dir_pages[i][ENTRY_DIR_PAGES].bits.rw = 1;
  dir_pages[i][ENTRY_DIR_PAGES].bits.present = 1;
  assigned_dir[i] = 0;
  // Es necessaria, just despr�s es fa un set_cr3. Si no es fa aix� peta.
  task[i].task.dir_pages_baseAddr = (page_table_entry *)&dir_pages[i][ENTRY_DIR_PAGES];

}

}

void init_pb() {
	int i;
	for (i = 0; i< NR_TASKS; i++) {
		program_breaks[i] = HEAPSTART<<12;
		pb_counter[i] = 0;
	}
}


/* Initializes the page table (kernel pages only) */
void init_table_pages()
{
  int i,j;
  /* reset all entries */
for (j=0; j< NR_TASKS; j++) {
  for (i=0; i<TOTAL_PAGES; i++)
    {
      pagusr_table[j][i].entry = 0;
    }
  /* Init kernel pages */
  for (i=1; i<NUM_PAG_KERNEL; i++) // Leave the page inaccessible to comply with NULL convention 
    {
      // Logical page equal to physical page (frame)
      pagusr_table[j][i].bits.pbase_addr = i;
      pagusr_table[j][i].bits.rw = 1;
      pagusr_table[j][i].bits.present = 1;
    }
}
}



/* Initialize pages for initial process (user pages) */
void set_user_pages( struct task_struct *task )
{
 int pag; 
 int new_ph_pag;
 page_table_entry * process_PT =  get_PT(task);


  /* CODE */
  for (pag=0;pag<NUM_PAG_CODE;pag++){
	new_ph_pag=alloc_frame();
  	process_PT[PAG_LOG_INIT_CODE_P0+pag].entry = 0;
  	process_PT[PAG_LOG_INIT_CODE_P0+pag].bits.pbase_addr = new_ph_pag;
  	process_PT[PAG_LOG_INIT_CODE_P0+pag].bits.user = 1;
  	process_PT[PAG_LOG_INIT_CODE_P0+pag].bits.present = 1;
  }
  
  /* DATA */ 
  for (pag=0;pag<NUM_PAG_DATA;pag++){
	new_ph_pag=alloc_frame();
  	process_PT[PAG_LOG_INIT_DATA_P0+pag].entry = 0;
  	process_PT[PAG_LOG_INIT_DATA_P0+pag].bits.pbase_addr = new_ph_pag;
  	process_PT[PAG_LOG_INIT_DATA_P0+pag].bits.user = 1;
  	process_PT[PAG_LOG_INIT_DATA_P0+pag].bits.rw = 1;
  	process_PT[PAG_LOG_INIT_DATA_P0+pag].bits.present = 1;
  }
}

/* Writes on CR3 register producing a TLB flush */
void set_cr3(page_table_entry * dir)
{
 	asm volatile("movl %0,%%cr3": :"r" (dir));
}

/* Macros for reading/writing the CR0 register, where is shown the paging status */
#define read_cr0() ({ \
         unsigned int __dummy; \
         __asm__( \
                 "movl %%cr0,%0\n\t" \
                 :"=r" (__dummy)); \
         __dummy; \
})
#define write_cr0(x) \
         __asm__("movl %0,%%cr0": :"r" (x));
         
/* Enable paging, modifying the CR0 register */
void set_pe_flag()
{
  unsigned int cr0 = read_cr0();
  cr0 |= 0x80000000;
  write_cr0(cr0);
}

/* Initializes paging for the system address space */
void init_mm()
{
  init_table_pages();
  init_frames();
  init_dir_pages();
  init_pb();
  set_cr3(get_DIR(&task[0].task));
  set_pe_flag();
}
/***********************************************/
/************** SEGMENTATION MANAGEMENT ********/
/***********************************************/
void setGdt()
{
  /* Configure TSS base address, that wasn't initialized */
  gdt[KERNEL_TSS>>3].lowBase = lowWord((DWord)&(tss));
  gdt[KERNEL_TSS>>3].midBase  = midByte((DWord)&(tss));
  gdt[KERNEL_TSS>>3].highBase = highByte((DWord)&(tss));

  gdtR.base = (DWord)gdt;
  gdtR.limit = 256 * sizeof(Descriptor);

  set_gdt_reg(&gdtR);
}

/***********************************************/
/************* TSS MANAGEMENT*******************/
/***********************************************/
void setTSS()
{
  tss.PreviousTaskLink   = NULL;
  tss.esp0               = KERNEL_ESP;
  tss.ss0                = __KERNEL_DS;
  tss.esp1               = NULL;
  tss.ss1                = NULL;
  tss.esp2               = NULL;
  tss.ss2                = NULL;
  tss.cr3                = NULL;
  tss.eip                = 0;
  tss.eFlags             = INITIAL_EFLAGS; /* Enable interrupts */
  tss.eax                = NULL;
  tss.ecx                = NULL;
  tss.edx                = NULL;
  tss.ebx                = NULL;
  tss.esp                = USER_ESP;
  tss.ebp                = tss.esp;
  tss.esi                = NULL;
  tss.edi                = NULL;
  tss.es                 = __USER_DS;
  tss.cs                 = __USER_CS;
  tss.ss                 = __USER_DS;
  tss.ds                 = __USER_DS;
  tss.fs                 = NULL;
  tss.gs                 = NULL;
  tss.LDTSegmentSelector = KERNEL_TSS;
  tss.debugTrap          = 0;
  tss.IOMapBaseAddress   = NULL;

  set_task_reg(KERNEL_TSS);
}

 
/* Initializes the ByteMap of free physical pages.
 * The kernel pages are marked as used */
int init_frames( void )
{
    int i;
    /* Mark pages as Free */
    for (i=0; i<TOTAL_PAGES; i++) {
        phys_mem[i] = FREE_FRAME;
    }
    /* Mark kernel pages as Used */
    for (i=0; i<NUM_PAG_KERNEL; i++) {
        phys_mem[i] = USED_FRAME;
    }
    return 0;
}

/* alloc_frame - Search a free physical page (== frame) and mark it as USED_FRAME. 
 * Returns the frame number or -1 if there isn't any frame available. */
int alloc_frame( void )
{
    int i;
    for (i=NUM_PAG_KERNEL; i<TOTAL_PAGES;) {
        if (phys_mem[i] == FREE_FRAME) {
            phys_mem[i] = USED_FRAME;
            return i;
        }
        i += 2; /* NOTE: There will be holes! This is intended. 
			DO NOT MODIFY! */
    }

    return -1;
}

void free_user_pages( struct task_struct *task )
{
 int pag;
 page_table_entry * process_PT =  get_PT(task);
    /* DATA */
 for (pag=0;pag<NUM_PAG_DATA;pag++){
	 free_frame(process_PT[PAG_LOG_INIT_DATA_P0+pag].bits.pbase_addr);
         process_PT[PAG_LOG_INIT_DATA_P0+pag].entry = 0;
 }
}


/* free_frame - Mark as FREE_FRAME the frame  'frame'.*/
void free_frame( unsigned int frame )
{
    /* You must insert code here */
    if ((frame>NUM_PAG_KERNEL)&&(frame<TOTAL_PAGES))
      phys_mem[frame]=FREE_FRAME;
}

/* set_ss_pag - Associates logical page 'page' with physical page 'frame' */
void set_ss_pag(page_table_entry *PT, unsigned page,unsigned frame)
{
	PT[page].entry=0;
	PT[page].bits.pbase_addr=frame;
	PT[page].bits.user=1;
	PT[page].bits.rw=1;
	PT[page].bits.present=1;

}

/* del_ss_pag - Removes mapping from logical page 'logical_page' */
void del_ss_pag(page_table_entry *PT, unsigned logical_page)
{
  PT[logical_page].entry=0;
}

/* get_frame - Returns the physical frame associated to page 'logical_page' */
unsigned int get_frame (page_table_entry *PT, unsigned int logical_page){
     return PT[logical_page].bits.pbase_addr; 
}

/* allocate_page_dir - Assignate Returns the physical frame associated to page 'logical_page' */
void allocate_page_dir (struct task_struct *p) {
	int i;
	char found = 0;
	for (i=0; i<NR_TASKS && !found; ++i) {
		found = (assigned_dir[i] == 0);
	}
	--i;
	p->dir_pages_baseAddr = (page_table_entry *)&dir_pages[i][ENTRY_DIR_PAGES];
	p->dir_count = &assigned_dir[i];
	assigned_dir[i] = 1;
}


void get_newpb (struct task_struct *p) {
	int i;
	char found = 0;
	for (i=0; i<NR_TASKS && !found; ++i) {
		found = (pb_counter[i] == 0);
	}
	--i;
	p->program_break = (unsigned int)&program_breaks[i];
	program_breaks[i] = HEAPSTART<<12;
	p->pb_count = &pb_counter[i];
	pb_counter[i] = 1;
}

