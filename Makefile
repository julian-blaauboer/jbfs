ifneq ($(KERNELRELEASE),)

obj-m = jbfs.o
jbfs-y = super.o inode.o dir.o file.o namei.o balloc.o ialloc.o

else

KDIR ?= /lib/modules/`uname -r`/build

all:
	make -C $(KDIR) M=$$PWD modules
clean:
	make -C $(KDIR) M=$$PWD clean
install:
	make -C $(KDIR) M=$$PWD modules_install

endif
