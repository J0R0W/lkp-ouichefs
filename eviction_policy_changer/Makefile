obj-m += eviction_policy_changer.o
KBUILD_EXTRA_SYMBOLS := $(src)/../Module.symvers

KERNELDIR = ../../../Linux_Vm/linux-6.5.7
SHARE_DIR = ../../../Linux_Vm/share
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
CHECK_PATCH=../checkpatch.pl --no-tree

all:
	make -C $(KERNELDIR) M=$(PWD) modules
	cp eviction_policy_changer.ko $(SHARE_DIR)/eviction_policy_changer.ko

debug:
	make -C $(KERNELDIR) M=$(PWD) ccflags-y+="-DDEBUG -g" modules
	cp eviction_policy_changer.ko $(SHARE_DIR)/eviction_policy_changer.ko

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
	rm -rf *~

check: 
	for f in *.c *.h ; do \
		$(CHECK_PATCH) -f $$f; \
	done

.PHONY: all clean
