obj-m += hbicap_fpga_manager/
obj-m += hwicap_fpga_manager/

SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f */*.o */*~ core */.depend */.*.cmd */*.ko */*.mod.c */*.mod
	rm -f Module.markers Module.symvers modules.order
	rm -f */modules.order
	rm -rf .tmp_versions Modules.symvers
