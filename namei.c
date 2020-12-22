#include <linux/fs.h>
#include "jbfs.h"

static int x = 0; // TODO: TESTING CODE REMOVE

static struct dentry *jbfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
  struct inode *inode = NULL;
  // TODO: TESTING CODE REMOVE
  if (strcmp(dentry->d_name.name, "foo") == 0 && x) {
    inode = jbfs_iget(dir->i_sb, 2);
  }
  return d_splice_alias(inode, dentry);
}

static int jbfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
  struct inode *inode;

  if (rdev)
    return -EINVAL;

  printk("jbfs: creating new file '%s'\n", dentry->d_name.name);
  if (strcmp(dentry->d_name.name, "foo") == 0)
    x = 1;

  // TODO: TESTING CODE REMOVE
  inode = jbfs_new_inode(dir, mode);
  if (IS_ERR(inode))
    return PTR_ERR(inode);

  jbfs_set_inode(inode, rdev);
  d_instantiate(dentry, inode);
  return 0;
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
