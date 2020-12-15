#include <linux/kernel.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/fs.h>
#include "jbfs.h"

struct inode *jbfs_iget(struct super_block *sb, unsigned long ino)
{
  struct inode *inode;
  struct jbfs_inode *raw_inode;
  struct jbfs_inode_info *jbfs_inode;
  struct jbfs_sb_info *sbi = JBFS_SB(sb);
  struct buffer_head *bh;
  uint64_t ino_group, ino_local, ino_pos;
  uint16_t nlinks;

  inode = iget_locked(sb, ino);
  if (!inode) {
    return ERR_PTR(-ENOMEM);
  }
  if (!(inode->i_state & I_NEW)) {
    return inode;
  }

  jbfs_inode = JBFS_I(inode);

  ino_group = ino >> sbi->s_local_inode_bits;
  ino_local = ino & (sbi->s_local_inode_bits - 1);
  ino_pos = sbi->s_offset_group + sbi->s_offset_inodes +
            ino_group * sbi->s_group_size +
            ino_local * sizeof(struct jbfs_inode);
  
  bh = sb_bread(sb, ino_pos / sb->s_blocksize);
  if (!bh) {
    printk(KERN_ERR "jbfs: Unable to read inode %lu.\n", ino);
    iget_failed(inode);
    return ERR_PTR(-EIO);
  }

  raw_inode = (struct jbfs_inode *) (((char *)bh->b_data) + ino_pos % sb->s_blocksize);
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
  // TODO: time
  inode->i_blocks = 0;

  brelse(bh);
  unlock_new_inode(inode);
  return inode;
}
