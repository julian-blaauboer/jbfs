#include <linux/buffer_head.h>
#include "jbfs.h"

static int jbfs_alloc_blocks_local(struct super_block *sb, uint64_t group, uint64_t local, int n, int *err)
{
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  struct buffer_head *bh;
  uint64_t block, offset;
  int i = 0;

  if (n <= 0) {
    return 0;
  }
  if (local >= sbi->s_group_data_blocks) {
    return 0;
  }

  block = sbi->s_offset_group + group * sbi->s_group_size + (local >> sbi->s_log_block_size);
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

      sb_bread(sb, block);
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

int jbfs_new_block(struct inode *inode)
{
  return -ENOSPC;
}
