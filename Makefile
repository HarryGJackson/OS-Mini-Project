obj-m += monitor.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: engine module

engine: engine.c
	gcc -Wall -o engine engine.c -pthread

module:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f engine
