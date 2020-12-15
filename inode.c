#include <linux/kernel.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include "jbfs.h"

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
  struct buffer_head *bh;
  uint16_t nlinks;

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
  jbfs_decode_time(&inode->i_ctime, le64_to_cpu(raw_inode->i_ctime));
  jbfs_decode_time(&inode->i_atime, le64_to_cpu(raw_inode->i_atime));
  jbfs_decode_time(&inode->i_mtime, le64_to_cpu(raw_inode->i_mtime));
  inode->i_blocks = 0;

  brelse(bh);
  unlock_new_inode(inode);
  return inode;
}

int jbfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
  struct buffer_head *bh;
  struct jbfs_inode *raw_inode; 
  int ret = 0;

  raw_inode = jbfs_raw_inode(inode->i_sb, inode->i_ino, &bh);
  if (!raw_inode) {
    return -EIO;
  }

  raw_inode->i_mode = cpu_to_le16(inode->i_mode);
  raw_inode->i_nlinks = cpu_to_le16(inode->i_nlink);
  raw_inode->i_uid = cpu_to_le16(fs_high2lowuid(i_uid_read(inode)));
  raw_inode->i_gid = cpu_to_le16(fs_high2lowuid(i_uid_read(inode)));
  raw_inode->i_size = cpu_to_le64(inode->i_size);
  raw_inode->i_ctime = cpu_to_le64(jbfs_encode_time(&inode->i_ctime));
  raw_inode->i_atime = cpu_to_le64(jbfs_encode_time(&inode->i_atime));
  raw_inode->i_mtime = cpu_to_le64(jbfs_encode_time(&inode->i_mtime));

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
