gpiod1: gpiod1.c
	arm-linux-gnueabihf-gcc gpiod1.c -lgpiod -L./libgpiod -I./libgpiod -o gpiod1
	cp gpiod1 ~/repo/embed_linux_tutorial/nfs_share

clean:
	rm -rf *.o gpiod1


.PHONY: clean  gpiod1 all
