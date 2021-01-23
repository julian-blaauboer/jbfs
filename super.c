// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 1991, 1992 Linus Torvalds
// Copyright (C) 2020, 2021 Julian Blaauboer

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/vfs.h>
#include <linux/iversion.h>
#include <linux/fs.h>
#include "jbfs.h"

static struct kmem_cache *jbfs_inode_cache;

static struct inode *jbfs_alloc_inode(struct super_block *sb)
{
	struct jbfs_inode_info *ji =
	    kmem_cache_alloc(jbfs_inode_cache, GFP_KERNEL);
	if (!ji) {
		return NULL;
	}

	inode_set_iversion(&ji->vfs_inode, 1);
	return &ji->vfs_inode;
}

static void jbfs_free_inode(struct inode *inode)
{
	kmem_cache_free(jbfs_inode_cache, JBFS_I(inode));
}

static void jbfs_put_super(struct super_block *sb)
{
	struct jbfs_sb_info *sbi = JBFS_SB(sb);

	sb->s_fs_info = NULL;
	brelse(sbi->s_sbh);
	kfree(sbi);
}

static const struct super_operations jbfs_sops = {
	.alloc_inode = jbfs_alloc_inode,
	.free_inode = jbfs_free_inode,
	.write_inode = jbfs_write_inode,
	.evict_inode = jbfs_evict_inode,
	.put_super = jbfs_put_super,
};

static int jbfs_sanity_check(struct jbfs_sb_info *sbi)
{
	const char *msg = "unknown error";
	if (sbi->s_offset_inodes < 2) {
		msg = "bitmap begins after inodes";
		goto fail;
	}
	if (sbi->s_offset_inodes >= sbi->s_offset_refmap) {
		msg = "inodes begin after refmap";
		goto fail;
	}
	if (sbi->s_offset_refmap >= sbi->s_offset_data) {
		msg = "refmap begins after data";
		goto fail;
	}
	if (sbi->s_offset_refmap >= sbi->s_offset_data) {
		msg = "data begins after end of group";
		goto fail;
	}
	if (sbi->s_offset_data + sbi->s_group_data_blocks > sbi->s_group_size) {
		msg = "data blocks don't fit within a group";
		goto fail;
	}
	return 1;
 fail:
	printk(KERN_ERR
	       "jbfs: inconsistent superblock (%s), refusing to mount\n", msg);
	return 0;
}

static int jbfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh;
	struct jbfs_sb_info *sbi;
	struct jbfs_super_block *js;
	struct inode *root_inode;
	int sb_block, sb_offset;
	int blocksize;
	int ret = -ENOMEM;
	int i;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi) {
		goto failed;
	}

	sb->s_fs_info = sbi;
	ret = -EINVAL;

	blocksize = sb_min_blocksize(sb, 1024);
	if (!blocksize) {
		printk(KERN_ERR "jbfs: unable to set blocksize.\n");
		goto failed_sbi;
	}

 reread_sb:
	sb_block = 1024 / blocksize;
	sb_offset = 1024 % blocksize;

	bh = sb_bread(sb, sb_block);
	if (!bh) {
		printk(KERN_ERR "jbfs: unable to read superblock.\n");
		goto failed_sbi;
	}

	js = (struct jbfs_super_block *)(((char *)bh->b_data) + sb_offset);
	sb->s_magic = le32_to_cpu(js->s_magic);

	if (sb->s_magic != JBFS_SUPER_MAGIC) {
		printk(KERN_ERR
		       "jbfs: magic doesn't match (expected 0x%08x, got 0x%08x).\n",
		       JBFS_SUPER_MAGIC, (int)sb->s_magic);
		goto failed_mount;
	}

	blocksize = 1 << le32_to_cpu(js->s_log_block_size);
	if (sb->s_blocksize != blocksize) {
		brelse(bh);

		if (!sb_set_blocksize(sb, blocksize)) {
			printk(KERN_ERR "jbfs: bad blocksize %d\n", blocksize);
			goto failed_sbi;
		}

		goto reread_sb;
	}

	sbi->s_js = js;
	sbi->s_sbh = bh;
	sbi->s_log_block_size = le32_to_cpu(js->s_log_block_size);
	sbi->s_flags = le64_to_cpu(js->s_flags);
	sbi->s_num_blocks = le64_to_cpu(js->s_num_blocks);
	sbi->s_num_groups = le64_to_cpu(js->s_num_groups);
	sbi->s_local_inode_bits = le32_to_cpu(js->s_local_inode_bits);
	sbi->s_group_size = le32_to_cpu(js->s_group_size);
	sbi->s_group_data_blocks = le32_to_cpu(js->s_group_data_blocks);
	sbi->s_group_inodes = le32_to_cpu(js->s_group_inodes);
	sbi->s_offset_group = le32_to_cpu(js->s_offset_group);
	sbi->s_offset_inodes = le32_to_cpu(js->s_offset_inodes);
	sbi->s_offset_refmap = le32_to_cpu(js->s_offset_refmap);
	sbi->s_offset_data = le32_to_cpu(js->s_offset_data);

	if (!jbfs_sanity_check(sbi))
		goto failed_mount;

	for (i = 0; i < JBFS_GROUP_N_LOCKS; ++i)
		mutex_init(&sbi->s_group_lock[i]);

	sb->s_op = &jbfs_sops;
	sb->s_time_min = 0;
	sb->s_time_max = 1ull << JBFS_TIME_SECOND_BITS;
	// TODO: Support i_cont
	sb->s_maxbytes =
	    12 * (sbi->s_group_data_blocks << sbi->s_log_block_size);

	root_inode = jbfs_iget(sb, 1);
	if (IS_ERR(root_inode)) {
		ret = PTR_ERR(root_inode);
		printk(KERN_ERR "jbfs: cannot get root inode.\n");
		goto failed_mount;
	}

	ret = -ENOMEM;
	sb->s_root = d_make_root(root_inode);
	if (sb->s_root) {
		return 0;
	}

 failed_mount:
	brelse(bh);
 failed_sbi:
	sb->s_fs_info = NULL;
	kfree(sbi);
 failed:
	return ret;
}

static struct dentry *jbfs_mount(struct file_system_type *fs_type, int flags,
				 const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, jbfs_fill_super);
}

static struct file_system_type jbfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "jbfs",
	.mount = jbfs_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV
};

static void init_once(void *ptr)
{
	struct jbfs_inode_info *ji = (struct jbfs_inode_info *)ptr;
	inode_init_once(&ji->vfs_inode);
}

static int __init jbfs_init(void)
{
	int ret;

	jbfs_inode_cache = kmem_cache_create("jbfs_inode_cache",
					     sizeof(struct jbfs_inode_info),
					     0,
					     (SLAB_RECLAIM_ACCOUNT |
					      SLAB_MEM_SPREAD | SLAB_ACCOUNT),
					     init_once);

	if (!jbfs_inode_cache) {
		return -ENOMEM;
	}

	ret = register_filesystem(&jbfs_fs_type);

	if (likely(ret == 0)) {
		printk(KERN_INFO "jbfs: registered jbfs.\n");
	} else {
		printk(KERN_ERR
		       "jbfs: failed to register jbfs. error code: %d\n", ret);
		kmem_cache_destroy(jbfs_inode_cache);
	}

	return ret;
}

static void __exit jbfs_exit(void)
{
	int ret;

	rcu_barrier();
	kmem_cache_destroy(jbfs_inode_cache);

	ret = unregister_filesystem(&jbfs_fs_type);

	if (likely(ret == 0)) {
		printk(KERN_INFO "jbfs: unregistered jbfs.\n");
	} else {
		printk(KERN_ERR
		       "jbfs: failed to unregister jbfs. error code: %d\n",
		       ret);
	}
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julian Blaauboer");
MODULE_DESCRIPTION("The JBFS filesystem");
MODULE_VERSION("0.1.0");
MODULE_ALIAS_FS("jbfs");

module_init(jbfs_init);
module_exit(jbfs_exit);
