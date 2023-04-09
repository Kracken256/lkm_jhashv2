obj-m += jhashv2_prng.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

test:
	sudo dmesg -C
	sudo insmod jhashv2_prng.ko
	sudo rmmod jhashv2_prng.ko
	dmesg