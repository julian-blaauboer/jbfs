#include <linux/buffer_head.h>
#include "jbfs.h"

static int jbfs_alloc_blocks_local(struct super_block *sb, uint64_t group, uint64_t local, int n, int *err)
{
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  struct buffer_head *bh;
  uint64_t block, offset;
  int i = 0;

  *err = 0;

  if (n <= 0) {
    *err = -EINVAL;
    return 0;
  }
  if (local >= sbi->s_group_data_blocks) {
    *err = -EINVAL;
    return 0;
  }

  block = sbi->s_offset_group + group * sbi->s_group_size + sbi->s_offset_refmap + (local >> sbi->s_log_block_size);
  offset = local & (sb->s_blocksize - 1);

  bh = sb_bread(sb, block);
  if (!bh) {
    *err = -EIO;
    return 0;
  }

  while (i < n) {
    if (((uint8_t*)bh->b_data)[offset])
      goto out;

    ((uint8_t*)bh->b_data)[offset] = 1;
    i += 1;

    if (++offset >= sb->s_blocksize) {
      offset = 0;
      block += 1;
      mark_buffer_dirty(bh);
      brelse(bh);

      bh = sb_bread(sb, block);
      if (!bh) {
        *err = -EIO;
        return i;
      }
    }
  }

out:
  mark_buffer_dirty(bh);
  brelse(bh);
  return i;
}

static int jbfs_alloc_blocks(struct super_block *sb, uint64_t start, int n, int *err)
{
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  uint64_t group, local;

  group = (start - sbi->s_offset_group) / sbi->s_group_size;
  local = (start - sbi->s_offset_group) % sbi->s_group_size - sbi->s_offset_data;

  return jbfs_alloc_blocks_local(sb, group, local, n, err);
}

static uint64_t jbfs_find_free_in_group(struct super_block *sb, uint64_t group, int n, int *err)
{
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  struct buffer_head *bh;
  int count = 0;
  uint64_t block;
  int offset = 0;
  int i;

  *err = 0;

  block = sbi->s_offset_group + group * sbi->s_group_size + sbi->s_offset_refmap;
  bh = sb_bread(sb, block);
  if (!bh)
    goto out_no_bh;
  
  for (i = 0; i < sbi->s_group_data_blocks; ++i) {
    uint8_t ref = ((uint8_t*)bh->b_data)[offset];

    if (!ref) {
      if (++count == n)
        break;
    } else {
      count = 0;
    }
    
    if (++offset == sb->s_blocksize) {
      offset = 0;
      brelse(bh);
      bh = sb_bread(sb, ++block);
      if (!bh)
        goto out_no_bh;
    }
  }

  brelse(bh);

  if (count == n)
    return sbi->s_offset_group + group * sbi->s_group_size + sbi->s_offset_data + i;
  
  *err = -ENOSPC;
  return 0;

out_no_bh:
  *err = -EIO;
  return 0;
}

static uint64_t jbfs_find_free(struct inode *inode, int n, int *err)
{
  struct super_block *sb = inode->i_sb;
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  uint64_t start, group, block;

  start = inode->i_ino >> sbi->s_local_inode_bits;
  group = start;

  do {
    // TODO: Check group descriptor
    block = jbfs_find_free_in_group(sb, group, n, err);
    if (block)
      break;

    if (++group >= sbi->s_num_groups)
      group = 0;
  } while (group != start);

  return block;
}

uint64_t jbfs_new_block(struct inode *inode, int *err)
{
  struct jbfs_inode_info *jbfs_inode = JBFS_I(inode);
  uint64_t start;
  int n = 0;
  int i;

  // TODO: Support i_cont
  for (i = 0; i < 12; ++i) {
    if (!jbfs_inode->i_extents[i][0])
      break;
  }

  if (i > 0) {
    n = jbfs_alloc_blocks(inode->i_sb, jbfs_inode->i_extents[i-1][1] + 1, 1, err);
    jbfs_inode->i_extents[i-1][1] += n;
    if (*err)
      goto out;
    if (n) {
      *err = 0;
      goto out;
    }
  }

  if (jbfs_inode->i_extents[i][0]) {
    *err = -ENOSPC;
    return 0;
  }

  start = jbfs_find_free(inode, 1, err);
  if (!start)
    return 0;

  n = jbfs_alloc_blocks(inode->i_sb, start, 1, err);
  if (!n)
    return 0;

  jbfs_inode->i_extents[i][0] = start;
  jbfs_inode->i_extents[i][1] = start;

  *err = 0;
out:
  // TODO: Update group descriptor
  mark_inode_dirty(inode);
  return jbfs_inode->i_extents[i][1];
}