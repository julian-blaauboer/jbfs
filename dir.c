#include <linux/fs.h>
#include "jbfs.h"

static int jbfs_readdir(struct file *file, struct dir_context *ctx)
{
  dir_emit(ctx, "foo", 3, 2, DT_UNKNOWN); // TODO: TESTING CODE REMOVE
  return 0;
}

const struct file_operations jbfs_dir_operations = {
  .llseek = generic_file_llseek,
  .read = generic_read_dir,
  .iterate_shared = jbfs_readdir,
  .fsync = generic_file_fsync
};
