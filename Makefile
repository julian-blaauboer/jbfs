obj-m = jbfs.o
jbfs-objs = super.o inode.o dir.o file.o namei.o balloc.o ialloc.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
