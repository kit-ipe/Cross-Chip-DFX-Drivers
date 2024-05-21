# Cross-Chip-DFX-Drivers

This repo contains ***FPGA Manager Drivers*** compatible with the FPGA subsystem in Linux for both ICAP controller IP cores from AMD ([HWICAP](https://www.xilinx.com/products/intellectual-property/axi_hwicap.html) and [HBICAP](https://www.xilinx.com/products/intellectual-property/axi-hbicap.html)).

## Building the Drivers

The source code for both FPGA Managers can either be copied into the Linux Kernel sources and built with them, or they can be built as external Kernel modules directly from this repo. The latter will be described below.

### Preconditions

- A compiler to build for the target platform (most likely AArch64)
- Kernel sources that allow to build external Kernel modules (e.g. from [here](https://github.com/Xilinx/linux-xlnx))

### Workflow

1. Set the compiler to be used
	```
	$ export CROSS_COMPILE=aarch64-linux-gnu-
	```

2. Prepare the Kernel sources
	```
	$ cd /path/to/kernel/sources
	$ make ARCH=arm64 xilinx_zynqmp_defconfig
	$ make ARCH=arm64 modules_prepare
	```

3. Build the FPGA Manager drivers
	```
	$ cd /path/of/this/repo
	$ make ARCH=arm64 MAKE=make KERNEL_SRC=/path/to/kernel/sources
	```
