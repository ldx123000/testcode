#include "vfs.h"
#include "pmm.h"
#include "util/string.h"
#include "spike_interface/spike_utils.h"


//
// alloc a file system abstract
//
struct fs * alloc_fs(int fs_type){
  struct fs * fs = (struct fs *)alloc_page();
  fs->fs_type = fs_type;
  return fs;
}

//
// alloc an inode
//
struct inode * alloc_inode(int in_type){
  struct inode * node = (struct inode *)alloc_page();
  node->in_type = in_type;
  return node;
}

//
// mount a file system to the device named "devname"
//
int vfs_mount(const char * devname, int (*mountfunc)(struct device *dev, struct fs **vfs_fs)){
  int ret;
  struct vfs_dev_t * pdev_t = NULL;
  // 1. find the device entry in vfs_device_list(vdev_list) named devname
  for ( int i = 0; i < MAX_DEV; ++ i ){
    pdev_t = vdev_list[i];
    if ( strcmp(pdev_t->devname, devname) == 0 )
      break;
  }
  if ( pdev_t == NULL )
    panic("vfs_mount: Cannot find the device entry!\n");

  // 2. get the device struct pointer to the device
  struct device * pdevice = pdev_t->dev;

  // 3. mount the specific file system to the device with mountfunc
  if ( ( ret = mountfunc(pdevice, &(pdev_t->fs)) ) == 0 )
    sprint("VFS: file system successfully mounted to %s\n", pdev_t->devname);

  return 0;
}

//
// vfs_get_root
//
int vfs_get_root(const char *devname, struct inode **root_store){
  int ret;
  struct vfs_dev_t * pdev_t = NULL;
  // 1. find the device entry in vfs_device_list(vdev_list) named devname
  for ( int i = 0; i < MAX_DEV; ++ i ){
    pdev_t = vdev_list[i];
    if ( strcmp(pdev_t->devname, devname) == 0 )
      break;
  }
  if ( pdev_t == NULL )
    panic("vfs_get_root: Cannot find the device entry!\n");

  // 2. call fs.fs_get_root()
  struct inode * rootdir = fsop_get_root(pdev_t->fs);
  if ( rootdir == NULL )
    panic("vfs_get_root: failed to get root dir inode!\n");
  *root_store = rootdir;
  return 0;
}

//
// vfs_open
//
int vfs_open(char *path, int flags, struct inode **inode_store){
  // get flags
  int writable = 0;
  int creatable = flags & O_CREATE;
  switch ( flags & MASK_FILEMODE ){ // 获取flags的低2位
    case O_RDONLY: break;
    case O_WRONLY:
    case O_RDWR:
      writable = 1; break;
    default:
      panic("fs_open: invalid open flags!\n");
  }

  // lookup the path, and create an related inode
  int ret;
  struct inode * node;
  ret = vfs_lookup(path, &node);

  // if the path belongs to the host device
  if ( ret == -1 ){ // use host device
    int kfd = host_open(path, flags);
    return kfd;
  }
  // 找到文件
  if ( ret == 0 ){
    
  }
  // 如果没有文件，可以创建，则创建新文件

  // 如果没有文件，且不可以创建，返回错误
  // 如果有文件，打开

  *inode_store = node;
  return 0;
}

/*
 * get_device: 根据路径，选择目录的inode存入node_store
 * path format (absolute path):
 *    Case 1: device:path
 *      <e.g.> ramdisk0:/fileinram.txt
 *    Case 2: path
 *      <e.g.> fileinhost.txt
 * @return
 *    -1:     host device
 *    else:   save device root-dir-inode into node_store
 */
int get_device(char *path, char **subpath, struct inode **node_store) {
  int colon = -1;
  for ( int i = 0; path[i] != '\0'; ++ i ){
    if ( path[i] == ':' ){
      colon = i; break;
    }
  }
  // Case 2: the host device is used by default
  if ( colon < 0 ){
    *subpath    = path;
    *node_store = NULL;
    return -1;
  }
  // Case 1: find the root node of the device in the vdev_list
  path[colon] = '\0'; // get device name
  *subpath = path + colon + 1;
  sprint("get device: %s\n", subpath);
  // get the root dir-inode of [the device named "path"]
  return vfs_get_root(path, node_store);
}

//
// lookup the path
//
int vfs_lookup(char *path, struct inode **node_store){
  int ret;
  struct inode * dir;
  ret = get_device(path, &path, &dir);
  // Case 1: use host device file system
  if ( ret == -1 ){
    * node_store = NULL;
    return -1;
  }
  // Case 2: use PKE file system, dir: device root-dir-inode
  // device:/.../...
  if ( *path != '/' ) // must start from the root dir '/'
    panic("vfs_lookup: invalid file path!\n");
  // given root-dir-inode, find the file-inode in $path
  ret = vop_lookup(dir, path, node_store);
  return ret;
}
