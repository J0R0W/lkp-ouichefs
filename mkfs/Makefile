IMG = ouichefs.img
VM_DIR = ../../../Linux_Vm

BIN ?= mkfs.ouichefs
IMG ?= test.img
IMGSIZE ?= 50

all: ${BIN}

${BIN}: mkfs-ouichefs.c
	gcc -Wall -o $@ $<

img: ${BIN}
	rm -rf ${IMG}
	dd if=/dev/zero of=${IMG} bs=1M count=${IMGSIZE}
	./${BIN} ${IMG}
	cp ${IMG} ${VM_DIR}/ouichefs.img
	sync

clean:
	rm -rf *~

mrproper: clean
	rm -rf ${BIN}

.PHONY: all clean mrproper img
