/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "vmm.h"
#include "pmm.h"
#include "spike_interface/spike_utils.h"

#define MAXARGS 10

typedef struct elf_info_t {
  spike_file_t *f;
  struct process *p;
} elf_info;

//
// the implementation of allocater. allocates memory space for later segment loading
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  elf_info *msg = (elf_info *)ctx->info;
  // We assume that size of proram segment is smaller than a page.
  kassert(size < PGSIZE);
  void *pa = alloc_page();
  if (pa == 0) panic("uvmalloc mem alloc falied\n");

  memset((void *)pa, 0, PGSIZE);
  user_vm_map((pagetable_t)msg->p->pagetable, elf_va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));

  return pa;
}

//
// actual file reading, using the spike file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  // call spike file utility
  return spike_file_pread(msg->f, dest, nb, offset);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;

  return EL_OK;
}

//
// load the elf segments to memory regions
//
elf_status elf_load(elf_ctx *ctx) {
  elf_prog_header ph_addr;
  int i, off;
  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory before loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;

    // record the vm region in proc->mapped_info
    int j;
    for( j=0; j<PGSIZE/sizeof(mapped_region); j++ )
      if( (process*)(((elf_info*)(ctx->info))->p)->mapped_info[j].va == 0x0 ) break;

    ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].va = ph_addr.vaddr;
    ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].npages = 1;
    if( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_EXECUTABLE) ){
      ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].seg_type = CODE_SEGMENT;
      sprint( "CODE_SEGMENT added at mapped info offset:%d\n", j );
    }else if ( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_WRITABLE) ){
      ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].seg_type = DATA_SEGMENT;
      sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j );
    }else
      panic( "unknown program segment encountered, segment flag:%d.\n", ph_addr.flags );

    ((process*)(((elf_info*)(ctx->info))->p))->total_mapped_region ++;
  }

  return EL_OK;
}

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
static size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(struct process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");

  sprint("Application: %s\n", arg_bug_msg.argv[0]);

  // elf loading
  elf_ctx elfloader;
  elf_info info;

  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
  info.p = p;
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // entry (virtual) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close host file
  spike_file_close( info.f );

  sprint("sp in load bincode: %p\n", p->trapframe->regs.sp);

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

//
// load the elf of shell commands
//
void load_shell_bincode_from_host_elf(char ** argv){
  // 1. specify the path of the shell command object
  char path[30] = "./obj/";
  strcat(path, user_va_to_pa(current->pagetable, argv[0]));
  sprint("Shell application: %s\n", path);

  // 2. re-alloc the current process
  // 2.1. save old argv
  char * oldargv[MAXARGS+1];
  int argc;
  for ( argc = 0; user_va_to_pa(current->pagetable, argv[argc])!=0; ++ argc ){
    oldargv[argc] = user_va_to_pa(current->pagetable, argv[argc]);
  }
  oldargv[argc] = 0;
  // 2.2. reallocate the process
  realloc_process(current->pid);
  // 2.3. load the arguments
  // ustack example ///////
  // [0x7fffeff8] echo
  // [0x7fffeff0] a
  // [0x7fffefe8] 0
  // [0x7fffefe0] 0x7fffefe8
  // [0x7fffefd8] 0x7fffeff0
  // [0x7fffefd0] 0x7fffeff8
  // //////////////////////
  // build ustack for argv
  void * sp = (void *)current->trapframe->regs.sp;

  for ( int i = 0; i <= argc; ++ i ){
    sp -= 8;
    if ( i != argc ){
      strcpy(user_va_to_pa(current->pagetable, sp), oldargv[i]);
    }else{  // add 0
      *(uint64*)user_va_to_pa(current->pagetable, sp) = (uint64)oldargv[i];
    }
    oldargv[i] = sp;
  }
  // build ustack pointer for argv
  for ( int i = 0; i <= argc; ++ i ){
    sp -= 8;
    *(uint64*)user_va_to_pa(current->pagetable, sp) = (uint64)oldargv[argc-i];
  }
  current->trapframe->regs.sp = (uint64)sp;
  current->trapframe->regs.a0 = argc;  // main function arg number
  current->trapframe->regs.a1 = (uint64)sp;
  
  // 3. load bincode from host elf
  // elf loading
  elf_ctx elfloader;
  elf_info info;

  info.f = spike_file_open(path, O_RDONLY, 0);
  info.p = current;
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // entry (virtual) address
  current->trapframe->epc = elfloader.ehdr.entry;

  // close host file
  spike_file_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", current->trapframe->epc);
}