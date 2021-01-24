# JBFS
JB FileSystem _(working title)_ for Linux.

### Disclaimer
This filesystem is a personal project, I have no intent for it to compete with actual serious filesystems,
this is my first attempt at writing Linux kernel code. That is not to say that I am not trying to make
it somewhat usable, but do not expect this filesystem to ever get better than any filesystem already
in use. It's main purpose is to be educational to me, and perhaps to others as well. 

## Release 0.1.0
Filesystem seems stable enough to allow myself to work on new features. Hopefully, the next release will
be a lot closer to 'usable' (but probably not 'useful').

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
- Non-linear directory format (maybe?).
### Longer-term
- Add support for disk quota.
- Add support for xattr.
- Add support for DAX.
- Official, stable documentation for the on-disk layout.
