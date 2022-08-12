#include "vfs.h"
#include "rfs.h"
#include "dev.h"
#include "pmm.h"
#include "util/string.h"
#include "spike_interface/spike_utils.h"

// global RAMDISK0 BASE ADDRESS
extern void * RAMDISK0_BASE_ADDR;

//
// rfs_init: called by fs_init
//
void rfs_init(void){
  int ret;
  if ( (ret = rfs_mount("ramdisk0")) != 0 )
    panic("failed: rfs: rfs_mount: %d.\n", ret);
}

int rfs_mount(const char * devname) {
  return vfs_mount(devname, rfs_do_mount);
}

/*
 * Mount VFS(struct fs)-RFS(struct rfs_fs)-RAM Device(struct device)
 *
 * ******** RFS MEM LAYOUT (112 BLOCKS) ****************
 *   superblock  |  inodes  |  bitmap  |  free blocks  *
 *     1 block   |    10    |     1    |     100       *
 * *****************************************************
 */
int rfs_do_mount(struct device * dev, struct fs ** vfs_fs){
  /*
   * 1. alloc fs structure
   * struct fs (vfs.h):
   *        union { struct rfs_fs rfs_info } fs_info: rfs_fs
   *        fs_type:  rfs (=0)
   *    function:
   *        fs_sync
   *        fs_get_root
   *        fs_unmount
   *        fs_cleanup
   */
  struct fs * fs = alloc_fs(RFS_TYPE); // set fs_type
  sprint("=============\nfs: %p\n", fs);
  /*
   * 2. alloc rfs_fs structure
   * struct rfs_fs (rfs.h):
   *      super:    rfs_superblock
   *      dev:      the pointer to the device (struct device * in dev.h)
   *      freemap:  a block of bitmap (free: 0; used: 1)
   *      dirty:    true if super/freemap modified
   *      buffer:   buffer for non-block aligned io
   */
  struct rfs_fs * prfs = fsop_info(fs, RFS_TYPE);

  // 2.1. set [prfs->dev] & [prfs->dirty]
  prfs->dev   = dev;
  prfs->dirty = 0;

  // 2.2. alloc [io buffer] (1 block) for rfs
  prfs->buffer = alloc_page();

  // 2.3. usually read [superblock] (1 block) from device to prfs->buffer
  //      BUT for volatile RAM Disk, there is no superblock remaining on disk
  int ret;

  //      build a new superblock
  prfs->super.magic   = RFS_MAGIC;
  prfs->super.size    = 1 + RFS_MAX_INODE_NUM + 1 + RFS_MAX_INODE_NUM*RFS_NDIRECT;
  prfs->super.nblocks = RFS_MAX_INODE_NUM * RFS_NDIRECT;  // only direct index blocks
  prfs->super.ninodes = RFS_MAX_INODE_NUM;

  //      write the superblock to RAM Disk0
  memcpy(prfs->buffer, &(prfs->super), RFS_BLKSIZE);      // set io buffer
  if ( (ret = rfs_w1block(prfs, RFS_BLKN_SUPER)) != 0 )   // write to device
    panic("RFS: failed to write superblock!\n");

  // 2.4. similarly, build an empty [bitmap] and write to RAM Disk0
  prfs->freemap = alloc_page();
  memset(prfs->freemap, 0, RFS_BLKSIZE);
  prfs->freemap[0] = 1;   // the first data block is used for root directory

  //      write the bitmap to RAM Disk0
  memcpy(prfs->buffer, prfs->freemap, RFS_BLKSIZE);       // set io buffer
  if ( (ret = rfs_w1block(prfs, RFS_BLKN_BITMAP)) != 0 )  // write to device
    panic("RFS: failed to write bitmap!\n");

  // 2.5. build inodes (inode -> prfs->buffer -> RAM Disk0)
  struct rfs_dinode * pinode = (struct rfs_dinode *)prfs->buffer;
  pinode->size   = 0;
  pinode->type   = T_FREE;
  pinode->nlinks = 0;
  pinode->blocks = 0;

  //      write disk inodes to RAM Disk0
  for ( int i = 1; i < prfs->super.ninodes; ++ i )
    if ( (ret = rfs_w1block(prfs, RFS_BLKN_INODE + i)) != 0 )
      panic("RFS: failed to write inodes!\n");

  //      build root directory inode (ino = 0)
  pinode->size     = sizeof(struct rfs_direntry);
  pinode->type     = T_DIR;
  sprint("rfs_do_mount: root dir node type: %d\n", pinode->type);
  pinode->nlinks   = 1;
  pinode->blocks   = 1;
  pinode->addrs[0] = RFS_BLKN_FREE;

  //      write root directory inode to RAM Disk0
  if ( (ret = rfs_w1block(prfs, RFS_BLKN_INODE)) != 0 )
    panic("RFS: failed to write root inode!\n");
  
  // 2.6. write root directory block
  struct rfs_direntry * pdir = (struct rfs_direntry *)prfs->buffer;
  pdir->inum = RFS_BLKN_INODE;
  strcpy(pdir->name, "/");

  //      write root directory block to RAM Disk0
  if ( (ret = rfs_w1block(prfs, RFS_BLKN_FREE)) != 0 )
    panic("RFS: failed to write root inode!\n");
  
  // 3. mount functions
  fs->fs_sync     = rfs_sync;
  fs->fs_get_root = rfs_get_root;
  fs->fs_unmount  = rfs_unmount;
  fs->fs_cleanup  = rfs_cleanup;

  *vfs_fs = fs;

  return 0;
}

int rfs_r1block(struct rfs_fs *rfs, int blkno){
  // dop_output: ((dev)->d_output(buffer, blkno))
  return dop_output(rfs->dev, rfs->buffer, blkno);
}

int rfs_w1block(struct rfs_fs *rfs, int blkno){
  // dop_input:  ((dev)->d_input(buffer, blkno))
  return dop_input(rfs->dev, rfs->buffer, blkno);
}

int rfs_sync(struct fs *fs){
  panic("RFS: rfs_sync unimplemented!\n");
  return 0;
}

//
// Return root inode of filesystem.
//
struct inode * rfs_get_root(struct fs * fs){
  sprint("Call rfs_get_root\n");
  struct inode * node;
  // get rfs pointer
  struct rfs_fs * prfs = fsop_info(fs, RFS_TYPE);
  // load the root inode
  int ret;
  if ( (ret = rfs_load_dinode(prfs, RFS_BLKN_INODE, &node)) != 0 )
    panic("RFS: failed to load root inode!\n");
  return node;
}

//
// RFS: load inode from disk (ino: inode number)
//
int rfs_load_dinode(struct rfs_fs *prfs, int ino, struct inode **node_store){
  struct inode * node;
  int ret;
  // load inode[ino] from disk -> rfs buffer -> dnode
  if ( (ret = rfs_r1block(prfs, RFS_BLKN_INODE)) != 0 )
    panic("RFS: failed to read inode!\n");
  struct rfs_dinode * dnode = (struct rfs_dinode *)prfs->buffer;
  
  // build inode according to dinode
  if ( (ret = rfs_create_inode(prfs, dnode, ino, &node)) != 0 )
    panic("RFS: failed to create inode from dinode!\n");
  *node_store = node;
  return 0;
}

/*
 * Create an inode in kernel according to din
 * 
 */
int rfs_create_inode(struct rfs_fs *prfs, struct rfs_dinode * din, int ino, struct inode **node_store){
  // 1. alloc an vfs-inode in memory
  struct inode * node = alloc_inode(RFS_TYPE);
  // 2. copy disk-inode data
  struct rfs_dinode * dnode = vop_info(node, RFS_TYPE);
  dnode->size = din->size;
  dnode->type = din->type;
  dnode->nlinks = din->nlinks;
  dnode->blocks = din->blocks;
  for ( int i = 0; i < RFS_NDIRECT; ++ i )
    dnode->addrs[i] = din->addrs[i];

  // 3. set inum, ref, in_fs, in_ops
  // sprint("rfs: %p\n=============\n", prfs);
  node->inum   = ino;
  node->ref    = 0;
  node->in_fs  = (struct fs *)prfs;
  sprint("rfs_create_inode: ino: %d type: %d\n", ino, dnode->type);
  node->in_ops = rfs_get_ops(dnode->type);

  *node_store = node;
  return 0;
}

int rfs_unmount(struct fs *fs){
  return 0;
}

void rfs_cleanup(struct fs *fs){
  return;
}

int rfs_opendir(struct inode *node, int open_flags){
  int fd = 0;
  return fd;
}

int rfs_openfile(struct inode *node, int open_flags){
  return 0;
}

int rfs_close(struct inode *node){
  int fd = 0;
  return fd;
}

int rfs_fstat(struct inode *node, struct fstat *stat){
  struct rfs_dinode * dnode = vop_info(node, RFS_TYPE);
  stat->st_mode   = dnode->type;
  stat->st_nlinks = dnode->nlinks;
  stat->st_blocks = dnode->blocks;
  stat->st_size   = dnode->size;
  return 0;
}

/*
 *
 * @param node: root dir node
 * @param path: absolute file path from root
 * @param node_store: store the file inode
 * @return
 *    0: the file is found
 *    1: the file is not found, need to be created
 */
int rfs_lookup(struct inode *node, char *path, struct inode **node_store){
  struct rfs_dinode * dnode = vop_info(node, RFS_TYPE);
  struct rfs_fs * prfs = fsop_info(node->in_fs, RFS_TYPE);
  sprint("rfs_lookup: dnode type: %d (T_DIR = 2)\n", dnode->type);
  sprint("rfs_lookup: path: %s\n", path);

  // 逐层解析
  if ( path[0] == '/' && path[1] == '\0' )
    panic("rfs_lookup: no file name specified!\n");

  if ( path[0] != '/' )
    panic("rfs_lookup: not an absolute path!\n");

  // rfs 一层目录
  path = path + 1;
  sprint("rfs_lookup: file name: %s\n", path);

  // 读入一个dir block，遍历direntry，查找filename
  struct rfs_direntry * de;

  int nde = RFS_BLKSIZE / sizeof(struct rfs_direntry);
  
  for ( int i = 0; i < dnode->blocks; ++ i ){
    de = (struct rfs_direntry *)dnode->addrs[i]; // 第 i 个block的基地址
    // 文件还剩多少字节
    nde = dnode->size - i * RFS_BLKSIZE;
    // 文件还剩多少个direntry
    if ( nde > RFS_BLKSIZE ){
      nde = RFS_BLKSIZE / sizeof(struct rfs_direntry);
    }else {
      nde = nde / sizeof(struct rfs_direntry);
    }
    for ( int j = 0; j < nde; ++ j ){
      sprint("rfs_lookup (%d, %s)\n", de[j].inum, de[j].name);
      if ( strcmp(de[j].name, path) == 0 ){
        // 找到文件了，inum = de[j].inum
        // 读入第inum块dinode到prfs->buffer
        int ret = rfs_r1block(prfs, de[j].inum);
        // 根据dinode构造inode
        ret = rfs_create_inode(prfs, (struct rfs_dinode *)prfs->buffer, de[j].inum, node_store);
        return 0;
      }
    }
  }
  return 1;
}


// The sfs specific DIR operations correspond to the abstract operations on a inode.
static const struct inode_ops rfs_node_dirops = {
  .vop_open               = rfs_opendir,
  .vop_close              = rfs_close,
  .vop_fstat              = rfs_fstat,
  // .vop_fsync                      = sfs_fsync,
  // .vop_namefile                   = sfs_namefile,
  // .vop_getdirentry                = sfs_getdirentry,
  // .vop_reclaim                    = sfs_reclaim,
  // .vop_gettype                    = sfs_gettype,
  .vop_lookup             = rfs_lookup,
};

// The sfs specific FILE operations correspond to the abstract operations on a inode.
static const struct inode_ops rfs_node_fileops = {
  .vop_open               = rfs_openfile,
  .vop_close              = rfs_close,
  // .vop_read                       = sfs_read,
  // .vop_write                      = sfs_write,
  .vop_fstat              = rfs_fstat,
  // .vop_fsync                      = sfs_fsync,
  // .vop_reclaim                    = sfs_reclaim,
  // .vop_gettype                    = sfs_gettype,
  // .vop_tryseek                    = sfs_tryseek,
  // .vop_truncate                   = sfs_truncfile,
};

const struct inode_ops * rfs_get_ops(int type){
  switch (type) {
    case T_DIR:
      return &rfs_node_dirops;
    case T_FILE:
      return &rfs_node_fileops;
  }
  panic("RFS: invalid file type: %d\n", type);
}