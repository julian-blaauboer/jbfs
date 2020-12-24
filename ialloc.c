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
        local += index;
        goto found;
      }

      brelse(bh);
    }

    if (++group >= sbi->s_num_groups)
      group = 0;
  } while (group != start);

  return ERR_PTR(-ENOSPC);

found:

  inode = new_inode(sb);
  ji = JBFS_I(inode);

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

  if (insert_inode_locked(inode) < 0) {
    make_bad_inode(inode);
    iput(inode);
    return ERR_PTR(-EIO);
  }

  inode_init_owner(inode, dir, mode);
  insert_inode_hash(inode);
  mark_inode_dirty(inode);

  printk(KERN_INFO "jbfs: allocated new inode %lu\n", inode->i_ino);

  return inode;
}