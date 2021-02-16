// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Julian Blaauboer

#include <linux/buffer_head.h>
#include "jbfs.h"

struct jbfs_raw_extent {
	__le64 start;
	__le64 end;
};

struct cont_node {
	__le64 length;
	__le64 next;
	struct jbfs_raw_extent extents[];
};

int jbfs_alloc_blocks_local(struct inode *inode, u64 *bno, int min, int max,
			    u64 group, u64 local)
{
	struct buffer_head *bh;
	struct super_block *sb = inode->i_sb;
	struct jbfs_sb_info *sbi = JBFS_SB(sb);
	int i = local & (sb->s_blocksize - 1);
	u64 best_start = local, best_n = 0, n = 0;
	u64 limit = sbi->s_group_data_blocks;
	u64 block = jbfs_group_refmap_start(sbi, group) +
		    (local >> inode->i_blkbits);

	bh = sb_bread(sb, block);
	if (!bh)
		return -EIO;

	if (group == sbi->s_num_groups - 1)
		limit = jbfs_block_extract_local(sbi, sbi->s_num_blocks - 1) + 1;

	for (; local < limit; ++local, ++i) {
		if (i == sb->s_blocksize) {
			i = 0;
			brelse(bh);
			bh = sb_bread(sb, ++block);
			if (!bh)
				return -EIO;
		}

		if (((u8 *)bh->b_data)[i]) {
			if (n > best_n) {
				best_start = local - n;
				best_n = n;
			}
			if (*bno)
				break;
			n = 0;
		} else {
			n += 1;
		}

		if (n >= max) {
			best_start = local - n + 1;
			best_n = n;
			break;
		}
	}

	brelse(bh);

	if (best_n < min)
		return -ENOSPC;

	*bno = jbfs_block_compose(sbi, group, best_start);

	local = best_start;
	i = local & (sb->s_blocksize - 1);

	block = jbfs_group_refmap_start(sbi, group) +
		(local >> inode->i_blkbits);

	bh = sb_bread(inode->i_sb, block);
	if (!bh)
		return -EIO;

	mark_buffer_dirty(bh);

	for (; local < best_start + best_n; ++local, ++i) {
		if (i == sb->s_blocksize) {
			i = 0;
			brelse(bh);
			bh = sb_bread(inode->i_sb, ++block);
			/* Note: Some of the blocks have already been allocated.
			 * We should try to free them, but we don't (yet).
			 */
			if (!bh)
				return -EIO;

			mark_buffer_dirty(bh);
		}

		((u8 *)bh->b_data)[i] = 1;
	}

	brelse(bh);
	return best_n;
}

int jbfs_alloc_blocks(struct inode *inode, u64 *bno, int min, int max)
{

	struct jbfs_sb_info *sbi = JBFS_SB(inode->i_sb);
	u64 group, local, start;
	int n;

	if (*bno) {
		group = jbfs_block_extract_group(sbi, *bno);
		local = jbfs_block_extract_local(sbi, *bno);

		JBFS_GROUP_LOCK(sbi, group);
		n = jbfs_alloc_blocks_local(inode, bno, min, max, group, local);
		JBFS_GROUP_UNLOCK(sbi, group);

		return n;
	}

	group = jbfs_inode_extract_group(sbi, inode->i_ino);
	start = group;

	do {
		JBFS_GROUP_LOCK(sbi, group);
		n = jbfs_alloc_blocks_local(inode, bno, min, max, group, 0);
		JBFS_GROUP_UNLOCK(sbi, group);

		if (n >= min)
			return n;

		if (++group > sbi->s_num_groups)
			group = 0;
	} while (group != start);

	return -ENOSPC;
}

int jbfs_alloc_extent(struct inode *inode, int n, u64 *bno,
		      struct jbfs_extent *extent)
{
	int size;

	if (jbfs_extent_empty(extent)) {
		size = jbfs_alloc_blocks(inode, &extent->start, 1, n);
		*bno = extent->start;
		if (size >= 1)
			extent->end = extent->start + size;
	} else {
		size = jbfs_alloc_blocks(inode, &extent->end, 0, n);
		*bno = extent->end;
		if (size >= 0)
			extent->end += size;
	}

	return size;
}

u64 jbfs_alloc_cont(struct inode *inode, struct buffer_head **bh, int *err)
{
	u64 bno = 0;
	int n = jbfs_alloc_blocks(inode, &bno, 1, 1);
	if (n < 1) {
		*err = n;
		return 0;
	}

	*bh = sb_bread(inode->i_sb, bno);
	if (!*bh) {
		*err = -EIO;
		return 0;
	}

	memset((*bh)->b_data, 0, (*bh)->b_size);
	mark_buffer_dirty(*bh);

	return bno;
}

int jbfs_new_blocks_cont(struct inode *inode, sector_t iblock, u64 *bno,
			 int max, struct buffer_head *bh,
			 struct jbfs_raw_extent *raw)
{
	struct super_block *sb = inode->i_sb;
	struct cont_node *node = (struct cont_node *)bh->b_data;
	struct buffer_head *next_bh;
	int size, err = 0;

	if (raw != node->extents)
		--raw;

	for(;;) {
		struct jbfs_raw_extent *end =
		    (struct jbfs_raw_extent *)(bh->b_data + sb->s_blocksize);

		mark_buffer_dirty(bh);

		for (; raw != end; ++raw) {
			struct jbfs_extent extent;

			extent.start = le64_to_cpu(raw->start);
			extent.end = le64_to_cpu(raw->end);

			size = jbfs_alloc_extent(inode, iblock + max, bno,
						 &extent);

			raw->start = cpu_to_le64(extent.start);
			raw->end = cpu_to_le64(extent.end);

			node->length = cpu_to_le64(le64_to_cpu(node->length) +
						   size);

			if (size < 0) {
				brelse(bh);
				return size;
			}

			if (size > iblock) {
				brelse(bh);
				*bno += iblock;
				return size - iblock;
			}

			iblock -= size;
		}

		node->next = cpu_to_le64(jbfs_alloc_cont(inode, &next_bh, &err));
		brelse(bh);
		if (err < 0)
			return err;

		bh = next_bh;
		node = (struct cont_node *)bh->b_data;
		raw = node->extents;
	}

	BUG();
	return -EINVAL;
}

int jbfs_new_blocks_local(struct inode *inode, sector_t iblock, u64 *bno,
			  int max, int i)
{
	struct jbfs_inode_info *ji = JBFS_I(inode);
	struct buffer_head *bh;
	struct cont_node *node;
	int size, err = 0;

	for (i = i ? i - 1 : i; i < JBFS_INODE_EXTENTS; ++i) {
		size = jbfs_alloc_extent(inode, iblock + max, bno,
					 &ji->i_extents[i]);

		if (size < 0)
			return size;

		if (size > iblock) {
			*bno += iblock;
			return size - iblock;
		}

		iblock -= size;
	}

	ji->i_cont = jbfs_alloc_cont(inode, &bh, &err);
	if (err < 0)
		return err;

	node = (struct cont_node *)bh->b_data;
	return jbfs_new_blocks_cont(inode, iblock, bno, max, bh,
				    node->extents);
}

int jbfs_get_blocks(struct inode *inode, sector_t iblock, u64 *bno, int max,
		    int create, bool *new, bool *boundary)
{
	struct super_block *sb = inode->i_sb;
	struct jbfs_inode_info *ji = JBFS_I(inode);
	struct buffer_head *bh;
	u64 cont = ji->i_cont;
	int size, i;
	int ret = 0;

	for (i = 0; i < JBFS_INODE_EXTENTS; ++i) {
		if (jbfs_extent_empty(&ji->i_extents[i]))
			break;

		size = jbfs_extent_size(&ji->i_extents[i]);
		if (size > iblock) {
			*bno = ji->i_extents[i].start + iblock;
			size -= iblock;
			if (size <= max) {
				*boundary = true;
				max = size;
			}
			return max;
		}
		iblock -= size;
	}

	if (!cont) {
		if (!create)
			return -EIO;

		ret = jbfs_new_blocks_local(inode, iblock, bno, max, i);
		if (ret > 0)
			*new = true;

		return ret;
	}

	do {
		struct cont_node *node;
		struct jbfs_raw_extent *raw, *end;
		struct jbfs_extent extent;

		bh = sb_bread(sb, cont);
		if (!bh)
			return -EIO;

		node = (struct cont_node *)bh->b_data;
		cont = le64_to_cpu(node->next);
		size = le64_to_cpu(node->length);

		end = (struct jbfs_raw_extent *)(bh->b_data + sb->s_blocksize);

		if (size >= iblock && cont) {
			iblock -= size;
			raw = end;
			brelse(bh);
			continue;
		}

		for (raw = node->extents; raw != end; ++raw) {
			extent.start = le64_to_cpu(raw->start);
			extent.end = le64_to_cpu(raw->end);

			if (jbfs_extent_empty(&extent))
				break;

			size = jbfs_extent_size(&extent);
			if (iblock < size) {
				*bno = extent.start + iblock;
				size -= iblock;
				if (size <= max) {
					*boundary = true;
					max = size;
				}
				brelse(bh);
				return max;
			}
			iblock -= size;
		}

		if (!cont) {
			if (!create)
				return -EIO;

			ret = jbfs_new_blocks_cont(inode, iblock, bno, max, bh,
						   raw);
			if (ret > 0)
				*new = true;

			return ret;
		}

		brelse(bh);
	} while (cont);

	return -EIO;
}

int jbfs_get_block(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create)
{
	struct jbfs_inode_info *ji = JBFS_I(inode);
	bool new = false, boundary = false;
	unsigned long max = bh_result->b_size >> inode->i_blkbits;
	u64 bno;
	int n;

	mutex_lock(&ji->i_mutex);
	n = jbfs_get_blocks(inode, iblock, &bno, max, create, &new, &boundary);
	mutex_unlock(&ji->i_mutex);
	if (n <= 0)
		return n;

	map_bh(bh_result, inode->i_sb, bno);
	bh_result->b_size = n << inode->i_blkbits;
	if (new)
		set_buffer_new(bh_result);
	if (boundary)
		set_buffer_boundary(bh_result);
	return 0;
}

static void jbfs_dealloc_blocks(struct super_block *sb, u64 start, u64 size)
{
	struct buffer_head *bh;
	struct jbfs_sb_info *sbi = JBFS_SB(sb);
	u64 group = jbfs_block_extract_group(sbi, start);
	u64 local = jbfs_block_extract_local(sbi, start);
	u64 limit = sbi->s_group_data_blocks;
	u64 block = jbfs_group_refmap_start(sbi, group) +
		    (local >> sb->s_blocksize_bits);
	int i = local & (sb->s_blocksize - 1);

	if (start + size > sbi->s_num_blocks)
		return;

	JBFS_GROUP_LOCK(sbi, group);

	bh = sb_bread(sb, block);
	if (!bh)
		goto out;

	mark_buffer_dirty(bh);

	for (; local < limit && size; ++local, ++i, --size) {
		if (i == sb->s_blocksize) {
			i = 0;
			brelse(bh);
			bh = sb_bread(sb, ++block);
			if (!bh)
				goto out;

			mark_buffer_dirty(bh);
		}

		((u8 *)bh->b_data)[i] -= 1;
	}

	brelse(bh);

out:
	JBFS_GROUP_UNLOCK(sbi, group);
}

void jbfs_truncate(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct jbfs_inode_info *ji = JBFS_I(inode);
	u64 cont = ji->i_cont;
	u64 blocks;
	u64 size;
	int i;

	blocks = (inode->i_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;

	block_truncate_page(inode->i_mapping, inode->i_size, jbfs_get_block);

	mutex_lock(&ji->i_mutex);

	for (i = 0; i < JBFS_INODE_EXTENTS && blocks; ++i) {
		if (jbfs_extent_empty(&ji->i_extents[i]))
			goto done;

		size = jbfs_extent_size(&ji->i_extents[i]);
		if (blocks >= size) {
			blocks -= size;
			continue;
		}

		ji->i_extents[i].end = ji->i_extents[i].start + blocks;
		jbfs_dealloc_blocks(sb, ji->i_extents[i].end, size - blocks);
	}

	for (; i < JBFS_INODE_EXTENTS; ++i) {
		if (jbfs_extent_empty(&ji->i_extents[i]))
			goto done;

		size = jbfs_extent_size(&ji->i_extents[i]);
		jbfs_dealloc_blocks(sb, ji->i_extents[i].start, size);
		ji->i_extents[i].start = ji->i_extents[i].end = 0;
	}

	if (!blocks)
		ji->i_cont = 0;

	while (cont) {
		struct buffer_head *bh;
		struct cont_node *node;
		struct jbfs_raw_extent *raw, *end;
		struct jbfs_extent extent;
		bool empty = !blocks;
		u64 next;
		u64 length;

		bh = sb_bread(sb, cont);
		if (!bh)
			goto done;

		mark_buffer_dirty(bh);

		node = (struct cont_node *)bh->b_data;
		end = (struct jbfs_raw_extent *)(bh->b_data + sb->s_blocksize);

		next = le64_to_cpu(node->next);
		length = le64_to_cpu(node->length);
		if (blocks >= length) {
			blocks -= length;
			goto done_cont;
		}

		for (raw = node->extents; raw != end && blocks; ++raw) {
			extent.start = le64_to_cpu(raw->start);
			extent.end = le64_to_cpu(raw->end);

			if (jbfs_extent_empty(&extent))
				goto done_cont;

			size = jbfs_extent_size(&extent);
			if (blocks >= size) {
				blocks -= size;
				continue;
			}

			extent.end = extent.start + blocks;
			jbfs_dealloc_blocks(sb, extent.end, size - blocks);

			raw->end = cpu_to_le64(extent.end);
		}

		for (; raw != end; ++raw) {
			extent.start = le64_to_cpu(raw->start);
			extent.end = le64_to_cpu(raw->end);

			if (jbfs_extent_empty(&extent))
				goto done_cont;

			size = jbfs_extent_size(&extent);
			jbfs_dealloc_blocks(sb, extent.start, size);

			raw->start = raw->end = 0;
			length -= size;
		}

done_cont:
		node->length = cpu_to_le64(length);

		if (!blocks)
			node->next = 0;

		brelse(bh);

		if (empty)
			jbfs_dealloc_blocks(sb, cont, 1);

		cont = next;
	}

done:
	mutex_unlock(&ji->i_mutex);
	inode->i_mtime = inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);
}
