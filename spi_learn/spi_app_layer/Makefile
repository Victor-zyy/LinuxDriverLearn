all: spidev1 gpiod1

gpiod1: gpiod1.c
	arm-linux-gnueabihf-gcc gpiod1.c -lgpiod -L./libgpiod -I./libgpiod -o gpiod1
	cp gpiod1 ~/repo/embed_linux_tutorial/nfs_share
spidev1: spidev1.c
	arm-linux-gnueabihf-gcc spidev1.c -o spidev1
	cp spidev1 ~/repo/embed_linux_tutorial/nfs_share


clean:
	rm -rf *.o gpiod1 spidev1


.PHONY: clean  gpiod1 all spidev1
