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

static int jbfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
  struct inode *inode;

  inode = jbfs_new_inode(dir, mode);

  if (IS_ERR(inode))
    return PTR_ERR(inode);

  jbfs_set_inode(inode, dev);
  mark_inode_dirty(inode);
  return add_nondir(dentry, inode);
}

static int jbfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
  return jbfs_mknod(dir, dentry, mode, 0);
}

static int jbfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
  struct inode *inode = d_inode(old_dentry);

  inode->i_ctime = current_time(inode);
  inode_inc_link_count(inode);
  ihold(inode);
  return add_nondir(dentry, inode);
}

static int jbfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
  struct inode *inode;
  int err;

  inode_inc_link_count(dir);

  inode = jbfs_new_inode(dir, S_IFDIR | mode);
  if (IS_ERR(inode)) {
    err = PTR_ERR(inode);
    goto out_dir;
  }

  jbfs_set_inode(inode, 0);

  inode_inc_link_count(inode);

  err = jbfs_make_empty(inode, dir);
  if (err)
    goto out_fail;

  err = jbfs_add_link(dentry, inode);
  if (err)
    goto out_fail;

  d_instantiate(dentry, inode);
out:
  return err;

out_fail:
  inode_dec_link_count(inode);
  inode_dec_link_count(inode);
  iput(inode);
out_dir:
  inode_dec_link_count(dir);
  goto out;
}

static int jbfs_symlink(struct inode *dir, struct dentry *dentry, const char *name)
{
  struct inode *inode;
  int len = strlen(name) + 1;
  int err;

  if (len > dir->i_sb->s_blocksize)
    return -ENAMETOOLONG;

  inode = jbfs_new_inode(dir, S_IFLNK | 0777);
  if (IS_ERR(inode))
    return PTR_ERR(inode);

  jbfs_set_inode(inode, 0);
  err = page_symlink(inode, name, len);
  if (err) {
    inode_dec_link_count(inode);
    iput(inode);
    return err;
  }

  return add_nondir(dentry, inode);
}

const struct inode_operations jbfs_dir_inode_operations = {
  .create = jbfs_create,
  .link = jbfs_link,
  .lookup = jbfs_lookup,
  .mkdir = jbfs_mkdir,
  .mknod = jbfs_mknod,
  .symlink = jbfs_symlink
};
