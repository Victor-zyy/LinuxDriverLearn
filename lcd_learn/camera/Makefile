
TOOLCHAIN = arm-linux-gnueabihf
CC	= $(TOOLCHAIN)-gcc
CXX = $(TOOLCHAIN)-g++
AR	= $(TOOLCHAIN)-ar
LD	= $(TOOLCHAIN)-ld
CPP = $(TOOLCHAIN)-cpp
COBJDUMP = $(TOOLCHAIN)-objdump

# GCC Flags and Include Dirs setting up

#INCLUDEDIR := $(shell pwd)/ /home/zyy/repo/embed_linux_tutorial/ebf_linux_kernel/build_image/build/include/config 
#INCLUDEDIR += /home/zyy/repo/embed_linux_tutorial/ebf_linux_kernel/build_image/build/include/generated
#INCLUDEDIR += /home/zyy/repo/embed_linux_tutorial/ebf_linux_kernel/include/
#INCLUDEDIR += /home/zyy/repo/embed_linux_tutorial/ebf_linux_kernel/build_image/build/usr/include/linux/

CPPFLAGS := $(addprefix -I, $(INCLUDEDIR))
CXXFLAGS := $(addprefix -I, $(CXXINCLUDEDIR))
CFLAGS := -Wall -O2

LDDIR	 := $(shell pwd)/pxp
LDFLAGS  := $(addprefix -L, $(LDDIR))

LDDLIB	 := pxp pthread
LDDLIBS  := $(addprefix -l, $(LDDLIB))


export CC AR LD CPP OBJDUMP CPPFLAGS

all: visionpxp visionvideo pxpv4l2 

%.o: %.S
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

visionpxp: visionpxp.o pxp/libpxp.a
	$(CC) $< $(LDFLAGS) $(CPPFLAGS) $(LDDLIBS) -o $@
	cp $@  ~/repo/embed_linux_tutorial/nfs_share

pxpv4l2: pxpv4l2.o
	$(CC) $< $(CPPFLAGS) -lpthread -o $@
	cp $@  ~/repo/embed_linux_tutorial/nfs_share

visionvideo: visionvideo.o
	$(CC) $< $(LDFLAGS) $(CPPFLAGS) -o $@
	cp $@  ~/repo/embed_linux_tutorial/nfs_share

pxp/libpxp.a:
	cd pxp; make; cd ..

clean:
	rm -rf *.o visionpxp visionvideo pxpv4l2
	cd pxp; make clean; cd ..

.PHONY: clean all