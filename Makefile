obj-m += oplus_bcc_boost.o
KDIR ?= $(PWD)/kernel
PWD := $(shell pwd)
all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=arm64 LLVM=1 modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
