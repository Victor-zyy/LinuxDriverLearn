# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y


# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
	DEBFLAGS = -O -g -DDEBUGSPIMODEL# "-O" is needed to expand inlines
else
	DEBFLAGS = -O2
endif

ccflags-y += $(DEBFLAGS)  # CFLAGS now changed to ccflags-y
ifneq ($(LDDINC),)
	ccflags-y += -I$(LDDINC) # LDDInclude Directory
endif

# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
# The module name and the source names must be different.
ifneq ($(KERNELRELEASE),)
	obj-m := spifbdev.o
else
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
#KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	ARCH=arm
	CROSS_COMPILE=arm-linux-gnueabihf-
	export ARCH CROSS_COMPILE
	KERNELDIR ?= /home/zyy/repo/embed_linux_tutorial/ebf_linux_kernel/build_image/build
	PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.order *.symvers
copy:
	mkdir -p /home/zyy/repo/embed_linux_tutorial/nfs_share/spi
	cp *.ko  /home/zyy/repo/embed_linux_tutorial/nfs_share/spi/

.PHONY:	modules_install modules clean copy


endif

