# JBFS
JB File System _(working title)_ for Linux.

### Disclaimer
This filesystem is a personal project, I have no intent for it to compete with actual serious filesystems,
this is my first attempt at writing Linux kernel code. That is not to say that I am not trying to make
it somewhat usable, but do not expect this filesystem to ever get better than any filesystem already
in use. It's main purpose is to be educational to me, and perhaps to others as well. 

## Release 0.1-alpha
The filesystem is currently in a working state (i.e., files can be added, removed, moved, linked etc.).
However, bugs are to be expected, documentation is absent and as a whole, the filesystem is extremely
fragile. Before adding any more features, I will dedicate my time to testing the code, adding _some_
documentation and formatting the code to be more in style with the Linux kernel. After that, I will
bump the version to 0.1.

## Planned features
### Short-term
- Better error logging than `printk`.
- Update and check group descriptors during inode allocation/deallocation.
- Add inode and block counts to super block (for `statfs`).
- Add `statfs`.
- Add support for the `i_cont` field to allow for more than 12 extents per inode, this requires reworking
  a significant part of the block (de)allocation algorithms.
### Long-term
- Add support for reflinks, this mostly requires implementing CoW.
- Add support for journaling (at least metadata).
### Longer-term
- Add support for disk quota.
- Add support for xattr.
- Add support for DAX.
- Official, stable documentation for the on-disk layout.
