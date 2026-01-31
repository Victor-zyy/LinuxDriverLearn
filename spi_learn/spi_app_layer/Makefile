TOOLCHAIN = arm-linux-gnueabihf
CC	= $(TOOLCHAIN)-gcc
AR	= $(TOOLCHAIN)-ar
LD	= $(TOOLCHAIN)-ld
CPP = $(TOOLCHAIN)-cpp
COBJDUMP = $(TOOLCHAIN)-objdump

# GCC Flags and Include Dirs setting up

CFLAGS := -Wall -O2
# multiple include dir 
INCLUDEDIR := $(shell pwd)/libgpiod $(shell pwd)/
CPPFLAGS := $(addprefix -I, $(INCLUDEDIR))
LDDIR	 := $(shell pwd)/libgpiod $(shell pwd)/oledgui $(shell pwd)/oledfonts
LDFLAGS  := $(addprefix -L, $(LDDIR))


export CC AR LD CPP OBJDUMP CPPFLAGS

all: spidev1 gpiod1 spioled1

gpiod1: gpiod1.c
	arm-linux-gnueabihf-gcc gpiod1.c -lgpiod -L./libgpiod -I./libgpiod -o gpiod1
	cp gpiod1 ~/repo/embed_linux_tutorial/nfs_share
spidev1: spidev1.c
	arm-linux-gnueabihf-gcc spidev1.c -o spidev1
	cp spidev1 ~/repo/embed_linux_tutorial/nfs_share

spioled1: spioled1.o oledfonts/libfont.a oledgui/libgui.a
	$(CC) spioled1.o -lgpiod -lfont -lgui $(LDFLAGS) $(CPPFLAGS) -o spioled1
	cp spioled1 ~/repo/embed_linux_tutorial/nfs_share

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
%.o: %.S
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

oledfonts/libfont.a:
	cd oledfonts; make; cd ..
oledgui/libgui.a: 
	cd oledgui; make; cd ..

clean:
	make clean -C oledfonts
	make clean -C oledgui
	rm -rf *.o gpiod1 spidev1 spioled1

.PHONY: clean  gpiod1 all spidev1 spioled1
