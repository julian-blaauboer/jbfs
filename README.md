# JBFS
JB File System _(working title)_ for Linux.

### Disclaimer
This filesystem is a personal project, I have no intent for it to compete with actual serious filesystems,
this is my first attempt at writing Linux kernel code. That is not to say that I am not trying to make
it somewhat usable, but do not expect this filesystem to ever get better than any filesystem already
in use. It's main purpose is to be educational to me, and perhaps to others as well. 

## Release 0.1.0-rc2
While documentation is still lacking, the most critical bugs are fixed. If I can't find any more bugs, I
will bump to 0.1.0 and start adding new features. Since rc1, the makefile has been improved and a relatively
small bug has been fixed.

## Planned features
### Short-term
- Add support for `O_DIRECT`.
- Better error logging than `printk`.
- Update and check group descriptors during inode allocation/deallocation.
- Add inode and block counts to super block (for `statfs`).
- Add `statfs`.
- Add UUID and label.
- Add support for the `i_cont` field to allow for more than 12 extents per inode, this requires reworking
  a significant part of the block (de)allocation algorithms.
### Long-term
- Add support for reflinks, this mostly requires implementing CoW.
- Add support for journaling (at least metadata).
- Non-linear directory format (maybe?).
### Longer-term
- Add support for disk quota.
- Add support for xattr.
- Add support for DAX.
- Official, stable documentation for the on-disk layout.
