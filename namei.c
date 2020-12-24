#include <linux/fs.h>
#include "jbfs.h"

static int add_nondir(struct dentry *dentry, struct inode *inode)
{
  int err = jbfs_add_link(dentry, inode);
  if (!err) {
    d_instantiate(dentry, inode);
    return 0;
  }
  inode_dec_link_count(inode);
  iput(inode);
  return err;
}

static struct dentry *jbfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
  struct inode *inode = NULL;
  ino_t ino;

  if (dentry->d_name.len > 255)
    return ERR_PTR(-ENAMETOOLONG);

  ino = jbfs_inode_by_name(dentry);
  if (ino)
    inode = jbfs_iget(dir->i_sb, ino);
  return d_splice_alias(inode, dentry);
}

static int jbfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
  struct inode *inode;

  if (rdev)
    return -EINVAL;

  inode = jbfs_new_inode(dir, mode);

  if (IS_ERR(inode))
    return PTR_ERR(inode);

  jbfs_set_inode(inode, rdev);
  mark_inode_dirty(inode);
  return add_nondir(dentry, inode);
}

static int jbfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
  return jbfs_mknod(dir, dentry, mode, 0);
}

const struct inode_operations jbfs_dir_inode_operations = {
  .create = jbfs_create,
  .lookup = jbfs_lookup,
  .mknod = jbfs_mknod
};
