// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 1991, 1992 Linus Torvalds
// Copyright (C) 1192, 1993, 1994, 1995 Remy Card
// Copyright (C) 2020, 2021 Julian Blaauboer

#include <linux/fs.h>
#include <linux/iversion.h>
#include "jbfs.h"

static int dir_check_page(struct page *page)
{
	struct inode *dir = page->mapping->host;
	struct super_block *sb = dir->i_sb;
	char *msg = "unknown error";
	unsigned end = PAGE_SIZE;
	unsigned i = 0;

	char *kaddr, *limit;
	struct jbfs_dirent *de;

	if ((dir->i_size >> PAGE_SHIFT) == page->index) {
		end = dir->i_size & ~PAGE_MASK;
		if (end & (sb->s_blocksize -1))
			goto bad_size;
		if (!end)
			goto out;
	}

	kaddr = page_address(page);
	limit = kaddr + end - JBFS_DIRENT_SIZE(1);
	de = (struct jbfs_dirent *)kaddr;
	while ((char *)de <= limit) {
		unsigned size = le16_to_cpu(de->d_size);
		if (unlikely(size < JBFS_DIRENT_SIZE(1)))
			goto tiny;
		if (unlikely(size & 7))
			goto unaligned;
		if (unlikely(size < JBFS_DIRENT_SIZE(de->d_len)))
			goto too_small;
		if (unlikely(((i + size - 1) ^ i) & ~(sb->s_blocksize-1)))
			goto span;

		de = (struct jbfs_dirent *)((char *)de + size);
		i += size;
	}
	if (i != sb->s_blocksize)
		goto out_of_bounds;

out:
	SetPageChecked(page);
	return 1;

bad_size:
	msg = "size not multiple of chunk size";
	goto out_err;
tiny:
	msg = "entry too tiny";
	goto out_err;
unaligned:
	msg = "misaligned entry";
	goto out_err;
too_small:
	msg = "entry too small for name";
	goto out_err;
span:
	msg = "entry spans multiple chunks";
	goto out_err;
out_of_bounds:
	msg = "entry exceeds page";
	goto out_err;

out_err:
	printk(KERN_ERR "jbfs: corrupted directory: %s\n", msg);
	SetPageError(page);
	return 0;
}

static inline void dir_put_page(struct page *page)
{
	kunmap(page);
	put_page(page);
}

static struct page *dir_get_page(struct inode *dir, unsigned long n)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (!IS_ERR(page)) {
		kmap(page);
		if (unlikely(!PageChecked(page))) {
			if (PageError(page) || !dir_check_page(page))
				goto out_err;
		}
	}
	return page;

out_err:
	dir_put_page(page);
	return ERR_PTR(-EIO);

}

static unsigned last_byte(struct inode *inode, unsigned long page_nr)
{
	unsigned last = inode->i_size - (page_nr << PAGE_SHIFT);
	return last > PAGE_SIZE ? PAGE_SIZE : last;
}

static int commit_chunk(struct page *page, loff_t pos, unsigned len)
{
	struct address_space *mapping = page->mapping;
	struct inode *dir = mapping->host;
	int err = 0;

	inode_inc_iversion(dir);
	block_write_end(NULL, mapping, pos, len, len, page, NULL);

	if (pos + len > dir->i_size) {
		i_size_write(dir, pos + len);
		mark_inode_dirty(dir);
	}

	if (IS_DIRSYNC(dir)) {
		err = write_one_page(page);
		if (!err)
			err = sync_inode_metadata(dir, 1);
	} else {
		unlock_page(page);
	}
	return err;
}

int jbfs_set_link(struct inode *dir, struct jbfs_dirent *de, struct page *page,
		  struct inode *inode)
{
	loff_t pos =
	    page_offset(page) + (char *)de - (char *)page_address(page);
	uint16_t size = le16_to_cpu(de->d_size);
	int err;

	lock_page(page);
	err = __block_write_begin(page, pos, size, jbfs_get_block);
	if (err)
		return err;

	de->d_ino = cpu_to_le64(inode->i_ino);

	err = commit_chunk(page, pos, size);
	dir_put_page(page);
	dir->i_mtime = dir->i_ctime = current_time(dir);
	mark_inode_dirty(dir);

	return err;
}

int jbfs_add_link(struct dentry *dentry, struct inode *inode)
{
	const char *name = dentry->d_name.name;
	int len_needed = dentry->d_name.len;
	uint16_t size_needed = JBFS_DIRENT_SIZE(len_needed);
	uint16_t size, min_size;
	uint8_t len;
	struct inode *dir = d_inode(dentry->d_parent);
	struct page *page = NULL;
	struct jbfs_dirent *de;
	uint64_t npages = dir_pages(dir);
	uint64_t n;
	loff_t pos;
	int err = 0;

	for (n = 0; n <= npages; ++n) {
		char *kaddr, *limit, *end;

		page = dir_get_page(dir, n);
		if (IS_ERR(page)) {
			printk(KERN_ERR "jbfs: bad page in inode %lu.\n",
			       dir->i_ino);
			err = PTR_ERR(page);
			goto out;
		}

		lock_page(page);

		kaddr = page_address(page);
		de = (struct jbfs_dirent *)kaddr;
		limit = kaddr + PAGE_SIZE - size_needed;
		end = kaddr + last_byte(dir, n);

		while ((char *)de <= limit) {
			if ((char *)de == end) {
				len = 0;
				size = dir->i_sb->s_blocksize;
				de->d_size = cpu_to_le16(size);
				de->d_ino = 0;
				goto got_it;
			}
			size = le16_to_cpu(de->d_size);
			len = de->d_len;
			if (size == 0) {
				printk(KERN_ERR
				       "jbfs: zero-length directory entry in inode %lu.\n",
				       dir->i_ino);
				err = -EIO;
				goto out_unlock;
			}
			if (de->d_ino && len == len_needed
			    && !memcmp(name, de->d_name, len)) {
				err = -EEXIST;
				goto out_unlock;
			}
			if (!de->d_ino && size >= size_needed) {
				goto got_it;
			}
			min_size = JBFS_DIRENT_SIZE(len);
			if (size >= size_needed + min_size) {
				goto got_it;
			}

			de = (struct jbfs_dirent *)((char *)de + size);
		}
		unlock_page(page);
		dir_put_page(page);
	}
	return -EINVAL;

 got_it:
	pos = page_offset(page) + (char *)de - (char *)page_address(page);
	err = __block_write_begin(page, pos, size, jbfs_get_block);
	if (err)
		goto out_unlock;

	if (de->d_ino) {
		struct jbfs_dirent *new_de =
		    (struct jbfs_dirent *)((char *)de + min_size);
		de->d_size = cpu_to_le16(min_size);
		new_de->d_size = cpu_to_le16(size - min_size);
		de = new_de;
	}

	de->d_ino = cpu_to_le64(inode->i_ino);
	de->d_len = len_needed;
	memcpy(de->d_name, name, len_needed);

	err = commit_chunk(page, pos, size);
	dir->i_mtime = dir->i_ctime = current_time(dir);
	mark_inode_dirty(dir);

 out_put:
	dir_put_page(page);
 out:
	return err;
 out_unlock:
	unlock_page(page);
	goto out_put;
}

int jbfs_empty_dir(struct inode *inode)
{
	struct page *page = NULL;
	uint64_t i, npages = dir_pages(inode);

	for (i = 0; i < npages; ++i) {
		char *kaddr;
		struct jbfs_dirent *de;

		page = dir_get_page(inode, i);
		if (IS_ERR(page)) {
			printk(KERN_ERR "jbfs: bad page in inode %lu.\n",
			       inode->i_ino);
			continue;
		}

		kaddr = page_address(page);
		de = (struct jbfs_dirent *)kaddr;
		kaddr += last_byte(inode, i) - JBFS_DIRENT_SIZE(1);

		while ((char *)de <= kaddr) {
			if (de->d_size == 0) {
				printk(KERN_ERR
				       "jbfs: zero-length directory entry in inode %lu.\n",
				       inode->i_ino);
				goto not_empty;
			}
			if (de->d_ino) {
				if (de->d_name[0] != '.')
					goto not_empty;
				if (de->d_len > 2)
					goto not_empty;
				if (de->d_len < 2) {
					if (le64_to_cpu(de->d_ino) !=
					    inode->i_ino)
						goto not_empty;
				} else if (de->d_name[1] != '.')
					goto not_empty;
			}
			de = (struct jbfs_dirent *)((char *)de +
						    le16_to_cpu(de->d_size));
		}
		dir_put_page(page);
	}
	return 1;

 not_empty:
	return 0;
}

int jbfs_make_empty(struct inode *inode, struct inode *parent)
{
	struct page *page = grab_cache_page(inode->i_mapping, 0);
	struct jbfs_dirent *de;
	unsigned chunk_size = inode->i_sb->s_blocksize;
	void *kaddr;
	int err;

	if (!page)
		return -ENOMEM;

	err = __block_write_begin(page, 0, chunk_size, jbfs_get_block);
	if (err) {
		unlock_page(page);
		goto out;
	}

	kaddr = kmap_atomic(page);
	memset(kaddr, 0, chunk_size);

	de = (struct jbfs_dirent *)kaddr;
	de->d_ino = cpu_to_le64(inode->i_ino);
	de->d_size = cpu_to_le16(16);
	de->d_len = 1;
	de->d_name[0] = '.';

	de = (struct jbfs_dirent *)((char *)kaddr + 16);
	de->d_ino = cpu_to_le64(parent->i_ino);
	de->d_size = cpu_to_le16(chunk_size - 16);
	de->d_len = 2;
	de->d_name[0] = '.';
	de->d_name[1] = '.';

	kunmap_atomic(kaddr);
	err = commit_chunk(page, 0, chunk_size);

 out:
	put_page(page);
	return err;
}

int jbfs_delete_entry(struct jbfs_dirent *dir, struct page *page)
{
	struct inode *inode = page->mapping->host;
	char *kaddr = page_address(page);
	int err = 0;

	uint64_t start =
	    ((char *)dir - kaddr) & ~(inode->i_sb->s_blocksize - 1);
	uint64_t end = (char *)dir - kaddr + le16_to_cpu(dir->d_size);
	loff_t pos;

	struct jbfs_dirent *prev = NULL;
	struct jbfs_dirent *de = (struct jbfs_dirent *)(kaddr + start);

	while (de < dir) {
		if (de->d_size == 0) {
			printk(KERN_ERR "jbfs: zero-length directory entry.\n");
			err = -EIO;
			goto out;
		}

		prev = de;
		de = (struct jbfs_dirent *)((char *)de +
					    le16_to_cpu(de->d_size));
	}

	if (prev)
		start = (char *)prev - kaddr;

	pos = page_offset(page) + start;
	lock_page(page);
	err = __block_write_begin(page, pos, end - start, jbfs_get_block);
	if (err)
		goto out;

	if (prev)
		prev->d_size = cpu_to_le16(end - start);

	dir->d_ino = 0;
	err = commit_chunk(page, pos, end - start);
	inode->i_ctime = inode->i_mtime = current_time(inode);
	mark_inode_dirty(inode);
 out:
	dir_put_page(page);
	return err;
}

struct jbfs_dirent *jbfs_find_entry(struct dentry *dentry,
				    struct page **res_page)
{
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	struct inode *dir = d_inode(dentry->d_parent);
	uint64_t npages = dir_pages(dir);
	uint64_t n;

	*res_page = NULL;

	for (n = 0; n < npages; ++n) {
		char *kaddr, *limit;
		struct jbfs_dirent *de;

		struct page *page = dir_get_page(dir, n);
		if (IS_ERR(page)) {
			printk(KERN_ERR "jbfs: bad page in inode %lu.\n",
			       dir->i_ino);
			return ERR_CAST(page);
		}

		kaddr = page_address(page);
		de = (struct jbfs_dirent *)kaddr;
		limit = kaddr + last_byte(dir, n) - JBFS_DIRENT_SIZE(1);

		while ((char *)de <= limit) {
			uint16_t size = le16_to_cpu(de->d_size);
			if (size == 0) {
				printk(KERN_ERR
				       "jbfs: zero-length directory entry in inode %lu.\n",
				       dir->i_ino);
				dir_put_page(page);
				return ERR_PTR(-EIO);
			}
			if (de->d_ino && de->d_len == len
			    && !memcmp(name, de->d_name, len)) {
				*res_page = page;
				return de;
			}
			de = (struct jbfs_dirent *)((char *)de + size);
		}
		dir_put_page(page);
	}

	return ERR_PTR(-ENOENT);
}

struct jbfs_dirent *jbfs_dotdot(struct inode *dir, struct page **p)
{
	struct jbfs_dirent *de = NULL;

	struct page *page = dir_get_page(dir, 0);
	if (!IS_ERR(page)) {
		de = (struct jbfs_dirent *)((char *)de +
					    le16_to_cpu(de->d_size));
		*p = page;
	}

	return de;
}

ino_t jbfs_inode_by_name(struct dentry *dentry)
{
	struct page *page;
	struct jbfs_dirent *de = jbfs_find_entry(dentry, &page);
	ino_t ino;

	if (!IS_ERR(de)) {
		ino = de->d_ino;
		dir_put_page(page);
		return ino;
	}

	return 0;
}

static int jbfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	uint64_t pos = ctx->pos;
	uint64_t npages = dir_pages(inode);
	uint32_t offset = pos & ~PAGE_MASK;
	uint64_t n = pos >> PAGE_SHIFT;

	if (pos > inode->i_size - JBFS_DIRENT_SIZE(1))
		return 0;

	for (; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		struct jbfs_dirent *de;

		struct page *page = dir_get_page(inode, n);
		if (IS_ERR(page)) {
			printk(KERN_ERR "jbfs: bad page in inode %lu.\n",
			       inode->i_ino);
			ctx->pos += PAGE_SIZE - offset;
			return PTR_ERR(page);
		}

		kaddr = page_address(page);
		de = (struct jbfs_dirent *)(kaddr + offset);
		limit = kaddr + last_byte(inode, n) - JBFS_DIRENT_SIZE(1);

		while ((char *)de <= limit) {
			uint16_t size = le16_to_cpu(de->d_size);
			if (size == 0) {
				printk(KERN_ERR
				       "jbfs: zero-length directory entry in inode %lu.\n",
				       inode->i_ino);
				dir_put_page(page);
				return -EIO;
			}
			if (de->d_ino) {
				if (!dir_emit
				    (ctx, de->d_name, de->d_len,
				     le32_to_cpu(de->d_ino), DT_UNKNOWN)) {
					dir_put_page(page);
					return 0;
				}
			}
			ctx->pos += size;
			de = (struct jbfs_dirent *)((char *)de + size);
		}
		dir_put_page(page);
	}

	return 0;
}

const struct file_operations jbfs_dir_operations = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.iterate_shared = jbfs_readdir,
	.fsync = generic_file_fsync
};
