#include "file.h"
#include "vfs.h"
#include "dev.h"
#include "rfs.h"
#include "pmm.h"
#include "riscv.h"
#include "process.h"
#include "util/functions.h"
#include "spike_interface/spike_file.h"
#include "spike_interface/spike_utils.h"

// ///////////////////////////////////
// Access to the RAM Disk
// ///////////////////////////////////

void fs_init(void){
  dev_init();
  rfs_init();
}

// //////////////////////////////////////////////////
// File operation interfaces provided to the process
// //////////////////////////////////////////////////

//
// open file
//
int do_open(char *pathname, int flags){
  // set file's readable & writable
  int readable = 0, writable = 0;
  switch ( flags & MASK_FILEMODE ){ // 获取flags的低2位
    case O_RDONLY: readable = 1; break;
    case O_WRONLY: writable = 1; break;
    case O_RDWR:
      readable = 1; writable = 1; break;
    default:
      panic("do_ram_open: invalid open flags!\n");
  }

  // find/create an 1.[inode] for the file in pathname
  struct inode * node;
  int ret;
  ret = vfs_open(pathname, flags, &node);

  // find a free 2.[file structure] in the PCB
  int fd = 0;
  struct file * pfile;

  // 2.1. Case 1: host device, ret := kfd
  if ( ret != 0 ){
    fd = ret;
    pfile = &(current->pfiles->ofile[fd]);
    pfile->status = FD_HOST;
    pfile->fd     = fd;
  }
  // 2.2. Case 2: PKE device
  else{
    // find a free file entry
    for ( ; fd < MAX_FILES; ++ fd ){
      pfile = &(current->pfiles->ofile[fd]);  // file entry
      if ( pfile->status == FD_NONE )         // free
        break;
    }
    if ( pfile->status != FD_NONE ) // no free entry
      panic("do_ram_open: no file entry for current process!\n");

    // initialize this file structure
    pfile->status = FD_OPENED;
    pfile->readable = readable;
    pfile->writable = writable;
    pfile->fd = fd;
    pfile->ref = 1;
    pfile->node = node;

    // use vop_fstat to get the file size from inode, and set the offset
    pfile->off = 0;
    struct fstat st; // file status
    if ( (ret = vop_fstat(node, &st)) != 0 ){
      panic("do_ram_open: failed to get file status!\n");
    }
    pfile->off = st.st_size;
  }

  ++ current->pfiles->nfile;
  return pfile->fd;
}

int do_read(int fd, char *buf, uint64 count){
  return -1;
}

int do_write(int fd, char *buf, uint64 count){
  return -1;
}

int do_close(int fd){
  return -1;
}

// ///////////////////////////////////
// Files struct in PCB
// ///////////////////////////////////

/*
 * initialize a files_struct for a process
 * struct files_struct * pfiles:
 *      cwd:      current working directory
 *      ofile:    array of file structure
 *      nfile:    * of opened files for current process
 */
struct files_struct * files_create(void){
  struct files_struct * pfiles = (struct files_struct *)alloc_page();
  pfiles->cwd   = NULL; // 将进程打开的第一个文件的目录作为进程的cwd
  pfiles->nfile = 0;
  // save file entries for spike files
  for ( int i = 0; i < MAX_FILES; ++ i ){
    if ( spike_files[i].kfd == -1 )
      continue;
    sprint("=====================\n");
    if ( i != spike_files[i].kfd ){
      sprint("file entry idx: %d, spike kfd: %d\n", i, spike_files[i].kfd);
    }
    sprint("refcnt: %d\n", spike_files[i].refcnt);
    sprint("=====================\n");
    pfiles->ofile[i].status = FD_HOST;
    pfiles->ofile[i].fd = spike_files[i].kfd;
    pfiles->ofile[i].ref = spike_files[i].refcnt;
    ++ pfiles->nfile;
  }
  // initialize other file entries
  for ( int i = 0; i < MAX_FILES; ++ i ){
    if ( pfiles->ofile[i].status != FD_HOST ){
      pfiles->ofile[i].status = FD_NONE;
      pfiles->ofile[i].ref = 0;
    }
  }
  sprint("FS: create a files_struct for process: nfile: %d\n", pfiles->nfile);
  return pfiles;
}

//
// destroy a files_struct for a process
//
void files_destroy(struct files_struct * pfiles){
  free_page(pfiles);
  return;
}

