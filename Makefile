obj-m += ouichefs.o
ouichefs-objs := fs.o super.o inode.o file.o dir.o

KERNELDIR = ../../Linux_Vm/linux-6.5.7
SHARE_DIR = ../../Linux_Vm/share
KERNELDIR ?= /lib/modules/$(shell uname -r)/build

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

.PHONY: all clean
