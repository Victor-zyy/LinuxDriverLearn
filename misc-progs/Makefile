FILES = nbtest load50 mapcmp polltest mapper setlevel setconsole inp outp \
	datasize dataalign netifdebug

COPY_DIR := /home/zyy/repo/embed_linux_tutorial/nfs_share/misc-progs
KERNELDIR ?=/lib/modules/$(shell uname -r)/build
INCLUDEDIR = $(KERNELDIR)/include
CFLAGS = -O2 -fomit-frame-pointer -Wall -I$(INCLUDEDIR)
all: $(FILES)

copy:
	mkdir -p $(COPY_DIR)
	cp *.c *.h $(COPY_DIR)

.PHONY: copy all
