// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 1991, 1992 Linus Torvalds
// Copyright (C) 2020 Julian Blaauboer

#include <linux/kernel.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include "jbfs.h"

int jbfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
{
  struct jbfs_inode_info *jbfs_inode;
  sector_t block;
  int ret = -EIO;
  int i;
  
  jbfs_inode = JBFS_I(inode);

  for (i = 0; i < 12; ++i) {
    uint64_t start = jbfs_inode->i_extents[i][0];
    uint64_t end   = jbfs_inode->i_extents[i][1];
    uint64_t len   = end - start + 1;
    if (!start)
      break;
    if (iblock < len) {
      block = start + iblock;
      ret = 0;
      break;
    }
    iblock -= len;
  }

  if (ret == 0)
    goto out;

  if (!create)
    return ret;

  while (iblock--) {
    jbfs_new_block(inode, &ret);
    if (ret)
      goto out_err;
  }

  block = jbfs_new_block(inode, &ret);
  if (ret)
    goto out_err;

  set_buffer_new(bh_result);
out:
  map_bh(bh_result, inode->i_sb, block);
  return 0;

out_err:
  printk(KERN_WARNING "jbfs: allocating new block for inode %lu failed with code %d\n", inode->i_ino, ret);
  return ret;
}

static int jbfs_writepage(struct page *page, struct writeback_control *wbc)
{
  return block_write_full_page(page, jbfs_get_block, wbc);
}

static int jbfs_readpage(struct file *file, struct page *page)
{
  return block_read_full_page(page, jbfs_get_block);
}

static void jbfs_write_failed(struct address_space *mapping, loff_t to)
{
  struct inode *inode = mapping->host;

  printk(KERN_ERR "jbfs: failed to write to inode %lu\n", inode->i_ino);

  if (to > inode->i_size) {
    truncate_pagecache(inode, inode->i_size);
    jbfs_truncate(inode);
  }
}

static int jbfs_write_begin(struct file *file, struct address_space *mapping, loff_t pos, unsigned len, unsigned flags, struct page **pagep, void **fsdata)
{
  int ret;

  ret = block_write_begin(mapping, pos, len, flags, pagep, jbfs_get_block);
  if (unlikely(ret))
    jbfs_write_failed(mapping, pos + len);

  return ret;
}

static sector_t jbfs_bmap(struct address_space *mapping, sector_t block)
{
  return generic_block_bmap(mapping, block, jbfs_get_block);
}

static const struct address_space_operations jbfs_aops = {
  .readpage = jbfs_readpage,
  .writepage = jbfs_writepage,
  .write_begin = jbfs_write_begin,
  .write_end = generic_write_end,
  .bmap = jbfs_bmap
};

static const struct inode_operations jbfs_symlink_inode_operations = {
  .get_link = page_get_link,
  .getattr = jbfs_getattr
};

static struct jbfs_inode *jbfs_raw_inode(struct super_block *sb, unsigned long ino, struct buffer_head **bh)
{
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  uint64_t group, local, pos;

  group = ino >> sbi->s_local_inode_bits;
  local = ino & ((1ull << sbi->s_local_inode_bits) - 1);
  pos   = (sbi->s_offset_group + sbi->s_offset_inodes +
          group * sbi->s_group_size) * sb->s_blocksize +
          (local - 1) * JBFS_INODE_SIZE;

  *bh = sb_bread(sb, pos / sb->s_blocksize);
  if (!*bh) {
    printk(KERN_ERR "jbfs: Unable to read inode %lu.\n", ino);
    return NULL;
  }

  return (struct jbfs_inode *) (((char *)(*bh)->b_data) + pos % sb->s_blocksize);
}

void jbfs_set_inode(struct inode *inode, dev_t dev)
{
  if (S_ISREG(inode->i_mode)) {
    inode->i_op = &jbfs_file_inode_operations;
    inode->i_fop = &jbfs_file_operations;
    inode->i_mapping->a_ops = &jbfs_aops;
  } else if (S_ISDIR(inode->i_mode)) {
    inode->i_op = &jbfs_dir_inode_operations;
    inode->i_fop = &jbfs_dir_operations;
    inode->i_mapping->a_ops = &jbfs_aops;
  } else if (S_ISLNK(inode->i_mode)) {
    inode->i_op = &jbfs_symlink_inode_operations;
    inode_nohighmem(inode);
    inode->i_mapping->a_ops = &jbfs_aops;
  } else {
    init_special_inode(inode, inode->i_mode, dev);
  }
}

struct inode *jbfs_iget(struct super_block *sb, unsigned long ino)
{
  struct inode *inode;
  struct jbfs_inode *raw_inode;
  struct jbfs_inode_info *jbfs_inode;
  struct jbfs_sb_info *sbi;
  struct buffer_head *bh;
  uint16_t nlinks;
  int i;

  sbi = JBFS_SB(sb);

  inode = iget_locked(sb, ino);
  if (!inode) {
    return ERR_PTR(-ENOMEM);
  }
  if (!(inode->i_state & I_NEW)) {
    return inode;
  }

  jbfs_inode = JBFS_I(inode);

  raw_inode = jbfs_raw_inode(sb, ino, &bh); 
  if (!raw_inode) {
    iget_failed(inode);
    return ERR_PTR(-EIO);
  }

  nlinks = le16_to_cpu(raw_inode->i_nlinks);
  if (nlinks == 0) {
    printk("jbfs: deleted inode referenced: %lu.\n", ino);
    brelse(bh);
    iget_failed(inode);
    return ERR_PTR(-ESTALE);
  }

  inode->i_mode = le16_to_cpu(raw_inode->i_mode);
  set_nlink(inode, le16_to_cpu(raw_inode->i_nlinks));
  i_uid_write(inode, le32_to_cpu(raw_inode->i_uid));
  i_gid_write(inode, le32_to_cpu(raw_inode->i_gid));
  jbfs_inode->i_flags = le32_to_cpu(raw_inode->i_flags);
  inode->i_size = le64_to_cpu(raw_inode->i_size);
  jbfs_decode_time(&inode->i_mtime, le64_to_cpu(raw_inode->i_mtime));
  jbfs_decode_time(&inode->i_atime, le64_to_cpu(raw_inode->i_atime));
  jbfs_decode_time(&inode->i_ctime, le64_to_cpu(raw_inode->i_ctime));
  jbfs_inode->i_cont = le64_to_cpu(raw_inode->i_cont);

  inode->i_blocks = 0;
  for (i = 0; i < 12; ++i) {
    jbfs_inode->i_extents[i][0] = le64_to_cpu(raw_inode->i_extents[i][0]);
    jbfs_inode->i_extents[i][1] = le64_to_cpu(raw_inode->i_extents[i][1]);
  }

  jbfs_set_inode(inode, new_decode_dev(raw_inode->i_extents[0][0]));

  brelse(bh);
  unlock_new_inode(inode);
  return inode;
}

int jbfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
  struct buffer_head *bh;
  struct jbfs_inode *raw_inode; 
  struct jbfs_inode_info *jbfs_inode;
  int ret = 0;
  int i;

  raw_inode = jbfs_raw_inode(inode->i_sb, inode->i_ino, &bh);
  if (!raw_inode) {
    printk(KERN_WARNING "jbfs: unable to get raw inode %lu.\n", inode->i_ino);
    return -EIO;
  }

  jbfs_inode = JBFS_I(inode);

  raw_inode->i_mode = cpu_to_le16(inode->i_mode);
  raw_inode->i_nlinks = cpu_to_le16(inode->i_nlink);
  raw_inode->i_uid = cpu_to_le16(fs_high2lowuid(i_uid_read(inode)));
  raw_inode->i_gid = cpu_to_le16(fs_high2lowuid(i_uid_read(inode)));
  raw_inode->i_size = cpu_to_le64(inode->i_size);
  raw_inode->i_flags = cpu_to_le64(jbfs_inode->i_flags);
  raw_inode->i_mtime = cpu_to_le64(jbfs_encode_time(&inode->i_mtime));
  raw_inode->i_atime = cpu_to_le64(jbfs_encode_time(&inode->i_atime));
  raw_inode->i_ctime = cpu_to_le64(jbfs_encode_time(&inode->i_ctime));
  if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
    raw_inode->i_extents[0][0] = cpu_to_le64(new_decode_dev(inode->i_rdev));
  else for (i = 0; i < 12; ++i) {
    raw_inode->i_extents[i][0] = cpu_to_le64(jbfs_inode->i_extents[i][0]);
    raw_inode->i_extents[i][1] = cpu_to_le64(jbfs_inode->i_extents[i][1]);
  }
  raw_inode->i_cont = cpu_to_le64(jbfs_inode->i_cont);

  mark_buffer_dirty(bh);
  if (wbc->sync_mode == WB_SYNC_ALL && buffer_dirty(bh)) {
    sync_dirty_buffer(bh);
    if (buffer_req(bh) && !buffer_uptodate(bh)) {
      printk(KERN_WARNING "jbfs: unable to sync inode %lu.\n", inode->i_ino);
      ret = -EIO;
    }
  }
  brelse(bh);
  return ret;
}

void jbfs_evict_inode(struct inode *inode)
{
  truncate_inode_pages_final(&inode->i_data);
  if (!inode->i_nlink) {
    inode->i_size = 0;
    jbfs_truncate(inode);
  }
  invalidate_inode_buffers(inode);
  clear_inode(inode);
  if (!inode->i_nlink)
    jbfs_delete_inode(inode);
}

int jbfs_getattr(const struct path *path, struct kstat *stat, u32 request_mask, unsigned int flags)
{
  struct super_block *sb = path->dentry->d_sb;
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  struct inode *inode = d_inode(path->dentry);
  struct jbfs_inode_info *ji = JBFS_I(inode);
  int i;

  generic_fillattr(inode, stat);

  // TODO: support i_cont
  stat->blocks = 0;
  for (i = 0; i < 12; ++i) {
    uint64_t start = ji->i_extents[i][0];
    uint64_t end   = ji->i_extents[i][1];
    stat->blocks += (end - start + !!start) << (sbi->s_log_block_size - 9);
  }

  stat->blksize = sb->s_blocksize;
  return 0;
}
