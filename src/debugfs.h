#ifndef __debugfs_cmds__h__
#define __debugfs_cmds__h__

#include <ext2fs/ext2fs.h>

#ifndef IO_BUFSIZE
#define IO_BUFSIZE 64 * 1024
#endif

extern int open_filesystem(char *device, char *io_options, blk64_t superblock, blk64_t blocksize);

extern int close_filesystem(void);
extern int fs_isClosed(void);
extern int fs_isReadWrite(void);

extern ext2_ino_t name_to_inode(const char *name);
extern __u16 inode_mode(const char *name);
extern __u16 modeType(__u16 mode);
extern __u16 ext2_fileType(__u16 mode);

extern int do_chmode(const char *name, mode_t mode);
extern int do_chmod(char *name, unsigned long mode);
extern int do_chown(const char *name, uid_t uid, gid_t gid);
extern int do_mkdir(struct stat *st, const char *name);
extern int do_chdir(const char *name);
extern int do_write(char *local_file, char *name);
extern int do_hardlink(struct stat *st, char *sourcename, const char *name);
extern int do_symlink(struct stat *st, char *source, char *dest);
extern int do_mknod(char *name, int type, unsigned long major, unsigned long minor);
extern int do_rm(char *name);

#endif
