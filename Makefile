obj-m += simple_fs.o

PWD := $(CURDIR) 
 
all: 
	make -C /lib/modules/5.10.0-28-arm64/build M=$(PWD) modules 
 
clean: 
	make -C /lib/modules/5.10.0-28-arm64/build M=$(PWD) clean
