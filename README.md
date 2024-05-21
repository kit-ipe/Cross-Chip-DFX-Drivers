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
	
## HWICAP FPGA Manager

The AXI Hardware Internal Configuration Access Port (HWICAP) IP core is Xilinx's light weight implementation of an ICAP controller. This IP core features a AXI4-Lite interface for data transfer.

### Example device tree entry

The following device tree excerpt shows the usage of the HWICAP FPGA Manager.

- `0x10_8001_0000` is the address of the HWICAP `S_AXI_LITE` interface

```
&amba_pl {
    axi_hwicap_0_client_0: axi_hwicap@1080010000 {
        compatible = "xlnx,hwicap-fpga";
        reg = <0x10 0x80010000 0x00 0x00001000>;
    };
};

&fpga_full {
    client_0: client_0 {
        compatible = "fpga-region";
        fpga-mgr = <&axi_hwicap_0_client_0>;
    };
};
```

## HBICAP FPGA Manager

The AXI High Bandwidth Internal Configuration Access Port (HBICAP) IP core is Xilinx's high performance implementation of an ICAP controller. This IP core features a full AXI4 interface for data transfer. The HBICAP FPGA Manager in this repo expects a AXI Central Direct Memory Access (CDMA) IP core to be used to write configuration data to the `S_AXI` data interface of the HBICAP IP core.

### Example device tree entry

The following device tree excerpt shows the usage of the HBICAP FPGA Manager.

- `0x10_8001_0000` is the address of the HBICAP `S_AXI_CTRL` interface
- `0x10_8001_1000` is the address of the HBICAP `S_AXI` interface
- `0x00_A000_0000` is the address of the AXI Central Direct Memory Access (CDMA) `S_AXI_LITE` interface

```
&amba_pl {
    axi_hbicap_0_client_0: axi_hbicap@1080010000 {
        compatible = "xlnx,hbicap-fpga";
        reg = <0x10 0x80010000 0x00 0x00001000>,<0x10 0x80011000 0x00 0x00001000>,<0x00 0xA0000000 0x00 0x00001000>;
    };
};

&fpga_full {
    client_0: client_0 {
        compatible = "fpga-region";
        fpga-mgr = <&axi_hbicap_0_client_0>;
    };
};
```
