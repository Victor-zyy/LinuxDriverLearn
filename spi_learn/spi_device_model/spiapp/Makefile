TOOLCHAIN = arm-linux-gnueabihf
CC	= $(TOOLCHAIN)-gcc
AR	= $(TOOLCHAIN)-ar
LD	= $(TOOLCHAIN)-ld
CPP = $(TOOLCHAIN)-cpp
COBJDUMP = $(TOOLCHAIN)-objdump

WORKPATH=$(shell pwd)
LDDPATH=$(shell pwd)/../../spi_app_layer

# GCC Flags and Include Dirs setting up

CFLAGS := -Wall -O2
# multiple include dir 
INCLUDEDIR := $(LDDPATH)/libgpiod $(LDDPATH)/ $(shell pwd)/
CPPFLAGS := $(addprefix -I, $(INCLUDEDIR))
LDDIR	 := $(LDDPATH)/libgpiod $(LDDPATH)/oledgui $(LDDPATH)/oledfonts
LDFLAGS  := $(addprefix -L, $(LDDIR))


export CC AR LD CPP OBJDUMP CPPFLAGS

all: spiapp 

spiapp: spiapp.o oledfonts/libfont.a oledgui/libgui.a
	$(CC) spiapp.o -lgpiod -lfont -lgui $(LDFLAGS) $(CPPFLAGS) -o spiapp
	cp spiapp ~/repo/embed_linux_tutorial/nfs_share

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
%.o: %.S
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

oledfonts/libfont.a:
	cd $(LDDPATH)/oledfonts; make; cd $(WORKPATH)
oledgui/libgui.a: 
	cd $(LDDPATH)/oledgui; make; cd $(WORKPATH)

clean:
	make clean -C $(LDDPATH)/oledfonts
	make clean -C $(LDDPATH)/oledgui
	rm -rf *.o spiapp

.PHONY: clean all 
