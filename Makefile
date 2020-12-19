obj-m = jbfs.o
jbfs-objs = super.o inode.o dir.o namei.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
