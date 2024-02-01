obj-m += ouichefs.o
ouichefs-objs := fs.o super.o inode.o file.o dir.o eviction_tracker.o

KERNELDIR = ../../Linux_Vm/linux-6.5.7
SHARE_DIR = ../../Linux_Vm/share
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
CHECK_PATCH=./checkpatch.pl --no-tree

all:
	make -C $(KERNELDIR) M=$(PWD) modules
	cp ouichefs.ko $(SHARE_DIR)/ouichefs.ko

debug:
	make -C $(KERNELDIR) M=$(PWD) ccflags-y+="-DDEBUG -g" modules
	cp ouichefs.ko $(SHARE_DIR)/ouichefs.ko

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
	rm -rf *~

setup: all
	make -C mkfs all img

check: 
	for f in *.c *.h ; do \
		$(CHECK_PATCH) -f $$f; \
	done

.PHONY: all clean
