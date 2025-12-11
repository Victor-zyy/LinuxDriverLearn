# Comment/uncomment the following line to disable/enable debugging
DEBUG = y


# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSCULL_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

ccflags-y += $(DEBFLAGS)  # CFLAGS now changed to ccflags-y
#ccflags-y += -I$(LDDINC) # LDDInclude Directory

# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := scull.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
#KERNELDIR ?= /lib/modules/$(shell uname -r)/build
else
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
	cp *.ko scull_load scull_unload /home/zyy/repo/embed_linux_tutorial/nfs_share/

.PHONY:	modules_install modules clean copy

endif
