#include <linux/kernel.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include "jbfs.h"

static int jbfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
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

  if (ret < 0)
    return ret;

  // TODO: Allocate new space

  map_bh(bh_result, inode->i_sb, block);
  return 0;
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

  if (to > inode->i_size) {
    truncate_pagecache(inode, inode->i_size);
    // TODO: Truncate allocation
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

static struct jbfs_inode *jbfs_raw_inode(struct super_block *sb, unsigned long ino, struct buffer_head **bh)
{
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  uint64_t group, local, pos;

  group = ino >> sbi->s_local_inode_bits;
  local = ino & ((1ull << sbi->s_local_inode_bits) - 1);
  pos   = (sbi->s_offset_group + sbi->s_offset_inodes +
          group * sbi->s_group_size) * sb->s_blocksize +
          (local - 1) * sizeof(struct jbfs_inode);

  *bh = sb_bread(sb, pos / sb->s_blocksize);
  if (!*bh) {
    printk(KERN_ERR "jbfs: Unable to read inode %lu.\n", ino);
    return NULL;
  }

  return (struct jbfs_inode *) (((char *)(*bh)->b_data) + pos % sb->s_blocksize);
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
    printk("jbfs: Deleted inode referenced: %lu.\n", ino);
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
    uint64_t start = le64_to_cpu(raw_inode->i_extents[i][0]);
    uint64_t end   = le64_to_cpu(raw_inode->i_extents[i][1]);
    jbfs_inode->i_extents[i][0] = start;
    jbfs_inode->i_extents[i][1] = end;
    inode->i_blocks += (end - start + !!start)  << sbi->s_log_block_size - 9;
  }

  if (S_ISREG(inode->i_mode)) {
    inode->i_mapping->a_ops = &jbfs_aops;
  } else if (S_ISDIR(inode->i_mode)) {
    inode->i_op = &jbfs_dir_inode_operations;
    inode->i_fop = &jbfs_dir_operations;
    inode->i_mapping->a_ops = &jbfs_aops;
  } else if (S_ISLNK(inode->i_mode)) {
    inode->i_mapping->a_ops = &jbfs_aops;
  } else {

  }

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
  for (i = 0; i < 12; ++i) {
    raw_inode->i_extents[i][0] = cpu_to_le64(jbfs_inode->i_extents[i][0]);
    raw_inode->i_extents[i][1] = cpu_to_le64(jbfs_inode->i_extents[i][1]);
  }
  raw_inode->i_cont = cpu_to_le64(jbfs_inode->i_cont);


  mark_buffer_dirty(bh);
  if (wbc->sync_mode == WB_SYNC_ALL && buffer_dirty(bh)) {
    sync_dirty_buffer(bh);
    if (buffer_req(bh) && !buffer_uptodate(bh)) {
      printk(KERN_WARNING "jbfs: Unable to sync inode %lu.\n", inode->i_ino);
      ret = -EIO;
    }
  }
  brelse(bh);
  return ret;
}
