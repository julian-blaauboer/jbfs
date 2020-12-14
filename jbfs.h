#ifndef JBFS_JBFS_H
#define JBFS_JBFS_H

#include <linux/buffer_head.h>

#define JBFS_SUPER_MAGIC 0x12050109
#define JBFS_TIME_SECOND_BITS 54
#define JBFS_LINK_MAX 65535
#define JBFS_SIZE_MAX 4096 // TODO

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
  __le64 i_ctime;
  __le64 i_atime;
  __le64 i_mtime;
  __le64 i_extents[12][2];
  __le64 i_cont;
};

struct jbfs_inode_info {
  uint32_t i_flags;
};

extern struct kmem_cache *jbfs_inode_cache;

#endif
