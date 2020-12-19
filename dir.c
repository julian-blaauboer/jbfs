#include <linux/fs.h>
#include "jbfs.h"

static int jbfs_readdir(struct file *file, struct dir_context *ctx)
{
  return 0;
}

const struct file_operations jbfs_dir_operations = {
  .llseek = generic_file_llseek,
  .read = generic_read_dir,
  .iterate_shared = jbfs_readdir,
  .fsync = generic_file_fsync
};
