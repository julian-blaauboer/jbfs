#include <linux/fs.h>
#include "jbfs.h"

static struct dentry *jbfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
  return d_splice_alias(NULL, dentry);
}

const struct inode_operations jbfs_dir_inode_operations = {
  .lookup = jbfs_lookup
};
