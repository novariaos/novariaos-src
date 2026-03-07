#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <core/fs/block.h>
#include <core/fs/vfs.h>

typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;

    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;

    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_padding1;

    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;

    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_reserved_char_pad;
    uint16_t s_reserved_word_pad;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;

    uint8_t  s_reserved[760];
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed)) ext2_bgd_t;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[1];
} __attribute__((packed)) ext2_dirent_t;


#define EXT2_MAGIC              0xEF53

#define EXT2_GOOD_OLD_REV       0
#define EXT2_DYNAMIC_REV        1

#define EXT2_GOOD_OLD_FIRST_INO 11
#define EXT2_GOOD_OLD_INODE_SIZE 128

#define EXT2_S_IFMT             0xF000
#define EXT2_S_IFSOCK           0xC000
#define EXT2_S_IFLNK            0xA000
#define EXT2_S_IFREG            0x8000
#define EXT2_S_IFBLK            0x6000
#define EXT2_S_IFDIR            0x4000
#define EXT2_S_IFCHR            0x2000
#define EXT2_S_IFIFO            0x1000

#define EXT2_ISREG(m)   (((m) & EXT2_S_IFMT) == EXT2_S_IFREG)
#define EXT2_ISDIR(m)   (((m) & EXT2_S_IFMT) == EXT2_S_IFDIR)
#define EXT2_ISLNK(m)   (((m) & EXT2_S_IFMT) == EXT2_S_IFLNK)

#define EXT2_FT_UNKNOWN         0
#define EXT2_FT_REG_FILE        1
#define EXT2_FT_DIR             2
#define EXT2_FT_CHRDEV          3
#define EXT2_FT_BLKDEV          4
#define EXT2_FT_FIFO            5
#define EXT2_FT_SOCK            6
#define EXT2_FT_SYMLINK         7

#define EXT2_ROOT_INO           2

#define EXT2_NDIR_BLOCKS        12
#define EXT2_IND_BLOCK          12
#define EXT2_DIND_BLOCK         13
#define EXT2_TIND_BLOCK         14

#define EXT2_VALID_FS           1
#define EXT2_ERROR_FS           2

typedef struct {
    block_device_t* block_dev;
    ext2_superblock_t sb;
    uint32_t block_size;
    uint32_t sectors_per_block;
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint32_t num_groups;
    uint32_t inode_size;
    uint32_t first_ino;
    ext2_bgd_t* bgdt;
    bool read_only;
} ext2_fs_t;

typedef struct {
    uint32_t ino;
    ext2_inode_t inode;
    uint32_t file_size;
} ext2_file_handle_t;

void ext2_init(void);
int ext2_mount(vfs_mount_t* mnt, const char* device, void* data);
int ext2_unmount(vfs_mount_t* mnt);

int         ext2_vfs_open   (vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h);
int         ext2_vfs_close  (vfs_mount_t* mnt, vfs_file_handle_t* h);
vfs_ssize_t ext2_vfs_read   (vfs_mount_t* mnt, vfs_file_handle_t* h, void*       buf, size_t count);
vfs_ssize_t ext2_vfs_write  (vfs_mount_t* mnt, vfs_file_handle_t* h, const void* buf, size_t count);
vfs_off_t   ext2_vfs_seek   (vfs_mount_t* mnt, vfs_file_handle_t* h, vfs_off_t offset, int whence);
int         ext2_vfs_readdir(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, size_t max_entries);
int         ext2_vfs_stat   (vfs_mount_t* mnt, const char* path, vfs_stat_t* stat);

#endif
