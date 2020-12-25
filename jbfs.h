// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Julian Blaauboer

#ifndef JBFS_JBFS_H
#define JBFS_JBFS_H

#include <linux/buffer_head.h>
#include <linux/fs.h>

#define JBFS_SUPER_MAGIC 0x12050109
#define JBFS_TIME_SECOND_BITS 54
#define JBFS_LINK_MAX 65535
#define JBFS_GROUP_N_LOCKS 32
#define JBFS_INODE_SIZE 256

#define JBFS_SB(sb) ((struct jbfs_sb_info *)sb->s_fs_info)

struct jbfs_super_block {
  __le32 s_magic;
  __le32 s_log_block_size;
  __le64 s_flags;
  __le64 s_num_blocks;
  __le64 s_num_groups;
  __le32 s_local_inode_bits;
  __le32 s_group_size;
  __le32 s_group_data_blocks;
  __le32 s_group_inodes;
  __le32 s_offset_group;
  __le32 s_offset_inodes;
  __le32 s_offset_refmap;
  __le32 s_offset_data;
  __le32 s_checksum;
};

struct jbfs_sb_info {
  struct jbfs_super_block *s_js;
  struct buffer_head *s_sbh;
  struct mutex s_group_lock[JBFS_GROUP_N_LOCKS];
  uint32_t s_log_block_size;
  uint64_t s_flags;
  uint64_t s_num_blocks;
  uint64_t s_num_groups;
  uint32_t s_local_inode_bits;
  uint32_t s_group_size;
  uint32_t s_group_data_blocks;
  uint32_t s_group_inodes;
  uint32_t s_offset_group;
  uint32_t s_offset_inodes;
  uint32_t s_offset_refmap;
  uint32_t s_offset_data;
};

#define JBFS_GROUP_LOCK(sbi, group) mutex_lock(&sbi->s_group_lock[group % JBFS_GROUP_N_LOCKS])
#define JBFS_GROUP_UNLOCK(sbi, group) mutex_unlock(&sbi->s_group_lock[group % JBFS_GROUP_N_LOCKS])

struct jbfs_group_descriptor {
  __le32 g_magic;
  __le32 g_free_inodes;
  __le32 g_free_blocks;
  __le32 g_checksum;
};

struct jbfs_inode {
  __le16 i_mode;
  __le16 i_nlinks;
  __le32 i_uid;
  __le32 i_gid;
  __le32 i_flags;
  __le64 i_size;
  __le64 i_mtime;
  __le64 i_atime;
  __le64 i_ctime;
  __le64 i_extents[12][2];
  __le64 i_cont;
};

struct jbfs_inode_info {
  uint32_t i_flags;
  uint64_t i_extents[12][2];
  uint64_t i_cont;
  struct inode vfs_inode;
};

struct jbfs_dirent {
  __le64 d_ino;
  __le16 d_size;
  __u8 d_len;
  char d_name[];
};

#define JBFS_DIRENT_SIZE(n) (11+n)

static inline struct jbfs_inode_info *JBFS_I(struct inode *inode)
{
  return container_of(inode, struct jbfs_inode_info, vfs_inode);
}

static inline uint64_t jbfs_encode_time(struct timespec64 *ts)
{
  return (ts->tv_sec << 10) + ts->tv_nsec / 1000000;
}

static inline void jbfs_decode_time(struct timespec64 *ts, uint64_t time)
{
  ts->tv_sec = time >> 10;
  ts->tv_nsec = (0x3ff & time) * 1000000;
}

int jbfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create);
void jbfs_set_inode(struct inode *inode, dev_t dev);
struct inode *jbfs_iget(struct super_block *sb, unsigned long ino);
int jbfs_write_inode(struct inode *inode, struct writeback_control *wbc);
void jbfs_evict_inode(struct inode *inode);

uint64_t jbfs_new_block(struct inode *inode, int *err);
void jbfs_truncate(struct inode *inode);

struct inode *jbfs_new_inode(struct inode *dir, umode_t mode);
int jbfs_delete_inode(struct inode *inode);

int jbfs_set_link(struct inode *dir, struct jbfs_dirent *de, struct page *page, struct inode *inode);
int jbfs_add_link(struct dentry *dentry, struct inode *inode);
struct jbfs_dirent *jbfs_find_entry(struct dentry *dentry, struct page **res_page);
int jbfs_delete_entry(struct jbfs_dirent *dir, struct page *page);
struct jbfs_dirent *jbfs_dotdot(struct inode *dir, struct page **p);
int jbfs_empty_dir(struct inode *inode);
int jbfs_make_empty(struct inode *inode, struct inode *parent);
ino_t jbfs_inode_by_name(struct dentry *dentry);

int jbfs_getattr(const struct path *path, struct kstat *stat, u32 request_mask, unsigned int flags);

extern const struct file_operations jbfs_dir_operations;
extern const struct inode_operations  jbfs_dir_inode_operations;
extern const struct file_operations jbfs_file_operations;
extern const struct inode_operations  jbfs_file_inode_operations;
#endif
