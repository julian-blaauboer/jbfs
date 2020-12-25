// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Julian Blaauboer

#include <linux/buffer_head.h>
#include <linux/bitops.h>
#include "jbfs.h"

struct inode *jbfs_new_inode(struct inode *dir, umode_t mode)
{
  struct super_block *sb = dir->i_sb;
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  struct inode *inode;
  struct jbfs_inode_info *ji;
  struct buffer_head *bh;
  uint64_t start, group;
  uint64_t block;
  uint32_t local, index;
  int i;

  start = dir->i_ino >> sbi->s_local_inode_bits;
  group = start;

  do {
    JBFS_GROUP_LOCK(sbi, group); 
    block = sbi->s_offset_group + group * sbi->s_group_size + 1;

    for (local = 0; local < sbi->s_group_inodes; local += sb->s_blocksize * 8) {
      bh = sb_bread(sb, block);
      if (!bh)
        continue;

      index = find_first_zero_bit((unsigned long*)bh->b_data, sb->s_blocksize * 8);
      if (index < sb->s_blocksize * 8) {
        set_bit(index, (unsigned long*)bh->b_data);
        mark_buffer_dirty(bh);
        brelse(bh);
        JBFS_GROUP_UNLOCK(sbi, group); 
        local += index;
        goto found;
      }

      brelse(bh);
    }

    JBFS_GROUP_UNLOCK(sbi, group); 
    if (++group >= sbi->s_num_groups)
      group = 0;
  } while (group != start);

  return ERR_PTR(-ENOSPC);

found:

  inode = new_inode(sb);
  ji = JBFS_I(inode);

  inode_init_owner(inode, dir, mode);

  inode->i_ino = (local + 1) | group << sbi->s_local_inode_bits;
  inode->i_blocks = 0;
  inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
  inode->i_mode = mode;

  ji->i_flags = 0;
  for (i = 0; i < 12; ++i) {
    ji->i_extents[i][0] = 0;
    ji->i_extents[i][1] = 0;
  }
  // TODO: Support i_cont
  ji->i_cont = 0;

  insert_inode_hash(inode);
  mark_inode_dirty(inode);

  return inode;
}

int jbfs_delete_inode(struct inode *inode)
{
  struct super_block *sb = inode->i_sb;
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  struct buffer_head *bh;
  uint64_t group = inode->i_ino >> sbi->s_local_inode_bits;
  uint64_t local = (inode->i_ino & ((1ull << sbi->s_local_inode_bits) - 1)) - 1;
  uint64_t block = sbi->s_offset_group + group * sbi->s_group_size + 1 + (local >> (sbi->s_log_block_size + 3));
  local &= sb->s_blocksize * 8 - 1;

  JBFS_GROUP_LOCK(sbi, group); 
  bh = sb_bread(sb, block);
  if (!bh)
    return -EIO;

  clear_bit(local, (unsigned long*)bh->b_data);
  mark_buffer_dirty(bh);
  brelse(bh);
  JBFS_GROUP_UNLOCK(sbi, group); 
  return 0;
}
