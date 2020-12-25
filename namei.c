// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 1991, 1992 Linus Torvalds
// Copyright (C) 1992, 1993, 1994, 1995 Remy Card
// Copyright (C) 2020 Julian Blaauboer

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
  struct inode *inode = jbfs_new_inode(dir, mode);

  if (IS_ERR(inode))
    return PTR_ERR(inode);

  jbfs_set_inode(inode, dev);
  mark_inode_dirty(inode);
  return add_nondir(dentry, inode);
}

static int jbfs_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
  struct inode *inode = jbfs_new_inode(dir, mode);

  if (IS_ERR(inode))
    return PTR_ERR(inode);

  jbfs_set_inode(inode, 0);
  mark_inode_dirty(inode);
  d_tmpfile(dentry, inode);
  return 0;
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

static int jbfs_unlink(struct inode *dir, struct dentry *dentry)
{
  struct inode *inode = d_inode(dentry);
  struct jbfs_dirent *de;
  struct page *page;
  int err;

  de = jbfs_find_entry(dentry, &page);
  if (IS_ERR(de))
    return PTR_ERR(de);

  err = jbfs_delete_entry(de, page);
  if (err)
    return err;

  inode->i_ctime = dir->i_ctime;
  inode_dec_link_count(inode);
  return 0;
  
}

static int jbfs_rename(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry, unsigned int flags)
{
  struct inode *old_inode = d_inode(old_dentry);
  struct inode *new_inode = d_inode(new_dentry);
  struct page *old_page = NULL;
  struct jbfs_dirent *old_de = NULL;
  struct page *dir_page = NULL;
  struct jbfs_dirent *dir_de = NULL;
  int err;

  if (flags & ~RENAME_NOREPLACE)
    return -EINVAL;

  old_de = jbfs_find_entry(old_dentry, &old_page);
  if (IS_ERR(old_de)) {
    err = PTR_ERR(old_de);
    goto out;
  }

  if (S_ISDIR(old_inode->i_mode)) {
    err = -EIO;
    dir_de = jbfs_dotdot(old_inode, &dir_page);
    if (!dir_de)
      goto out_old;
  }

  if (new_inode) {
    struct page *new_page;
    struct jbfs_dirent *new_de;

    err = -ENOTEMPTY;
    if (dir_de && !jbfs_empty_dir(new_inode))
      goto out_dir;

    new_de = jbfs_find_entry(new_dentry, &new_page);
    if (IS_ERR(new_de)) {
      err = PTR_ERR(new_de);
      goto out_dir;
    }

    jbfs_set_link(new_dir, new_de, new_page, old_inode);
    new_inode->i_ctime = current_time(new_inode);
    if (dir_de)
      drop_nlink(new_inode);
    inode_dec_link_count(new_inode);
  } else {
    err = jbfs_add_link(new_dentry, old_inode);
    if (err)
      goto out_dir;
    if (dir_de)
      inode_inc_link_count(new_dir);
  }

  old_inode->i_ctime = current_time(old_inode);
  jbfs_delete_entry(old_de, old_page);
  mark_inode_dirty(old_inode);


  if (dir_de) {
    jbfs_set_link(old_inode, dir_de, dir_page, new_dir);
    inode_dec_link_count(old_dir);
  }

out_dir:
  if (dir_de) {
    kunmap(dir_page);
    put_page(dir_page);
  }
out_old:
  kunmap(old_page);
  put_page(old_page);
out:
  return err;
}


static int jbfs_rmdir(struct inode *dir, struct dentry *dentry)
{
  struct inode *inode = d_inode(dentry);
  int err = -ENOTEMPTY;

  if (jbfs_empty_dir(inode)) {
    err = jbfs_unlink(dir, dentry);
    if (!err) {
      inode->i_size = 0;
      inode_dec_link_count(inode);
      inode_dec_link_count(dir);
    }
  }
  return err;
}

const struct inode_operations jbfs_dir_inode_operations = {
  .create = jbfs_create,
  .getattr = jbfs_getattr,
  .link = jbfs_link,
  .lookup = jbfs_lookup,
  .mkdir = jbfs_mkdir,
  .mknod = jbfs_mknod,
  .rename = jbfs_rename,
  .rmdir = jbfs_rmdir,
  .symlink = jbfs_symlink,
  .tmpfile = jbfs_tmpfile,
  .unlink = jbfs_unlink
};
