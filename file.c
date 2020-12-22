#include "jbfs.h"

static int jbfs_setattr(struct dentry *dentry, struct iattr *attr)
{
  struct inode *inode = d_inode(dentry);
  int err;

  err = setattr_prepare(dentry, attr);
  if (err)
    return err;

  if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size != i_size_read(inode)) {
    err = inode_newsize_ok(inode, attr->ia_size);
    if (err)
      return err;

    truncate_setsize(inode, attr->ia_size);
    // TODO: Truncate
  }

  setattr_copy(inode, attr);
  mark_inode_dirty(inode);
  return 0;
}

const struct file_operations jbfs_file_operations = {
  .llseek = generic_file_llseek,
  .read_iter = generic_file_read_iter,
  .write_iter = generic_file_write_iter,
  .mmap = generic_file_mmap,
  .fsync = generic_file_fsync,
  .splice_read = generic_file_splice_read
};

const struct inode_operations jbfs_file_inode_operations = {
  .setattr = jbfs_setattr,
  .getattr = jbfs_getattr,
};
