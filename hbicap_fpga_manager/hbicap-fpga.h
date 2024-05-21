/**
* FPGA Manager driver for the AXI HBICAP
*
* This driver enables partial reconfiguration through the Linux FPGA Subsystem with the AXI HBICAP.
* The FPGA firmware consists of the AXI HBICAP ICAP controller and the AXI CDMA IP core.
* In multi-board setups, the AXI CDMA is implemented on the host board and the AXI HBICAP on the client board.
* The reconfiguration is based on direct memory access. The partial bitstream is loaded in chunks of 4k into
* the PS DDR and then send to the AXI data port of the AXI HBICAP through the AXI CDMA.
*
* hbicap-fpga.c contains the prob-function, the fpga-manager ops and the setup function for the AXI HBICAP
* axi-hbicap.c contains the low level functions to access the AXI lite control registers of the AXI HBICAP
* axi-cdma.c contains the low level functions to access the AXI lite control registers of the AXI CDMA
*
* TODO: The AXI CDMA functions should be implemented into a separate driver that is called by the
* HBICAP FPGA manager driver. There are also probably some errors with the resource management.
* Especially with freeing the resources.
*
* NOTES: The driver is originally based on an outdated character device driver for the AXI HWICAP. This
* Driver was first modified to work with the newest version of the IP core and later rewritten into a FPGA
* Manager driver. For the AXI HBICAP Manager driver, a lot of unneeded an probably outdated functions were
* removed. In case of trouble, look at the original driver.
* This driver is also only tested with an AXI HBICAP that is configured with a write FIFO depth of 256 and
* and AXI, AXI Lite and ICAP clock of 100 MHz (all the same clock). They may be errors with a different
* configuration.
*
* Usefull links
* https://www.kernel.org/doc/html/latest/driver-api/fpga/fpga-mgr.html
* https://github.com/Xilinx/linux-xlnx/blob/master/Documentation/devicetree/bindings/fpga/fpga-region.txt
* https://www.kernel.org/doc/html/latest/core-api/dma-api-howto.html
**/
#ifndef HWICAP_FPGA_H_    /* prevent circular inclusions */
#define HWICAP_FPGA_H_    /* by using protection macros */

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#include <linux/io.h>

// HBICAP driver data structure
struct hbicap_drvdata {
    resource_size_t axi_lite_phys_base_addr;    /* phys. address of the AXI Lite control registers */
    resource_size_t axi_lite_phys_end_addr;     /* phys. address of the AXI Lite control registers */
    resource_size_t axi_lite_size;              /* AXI Lite control register size*/
    void __iomem *axi_lite_virt_base_addr;      /* virt. address of the AXI Lite control registers */

    u32 axi_data_phys_base_lower;               /* phys. address of the AXI data registers (lower 32 bit)*/
    u32 axi_data_phys_base_higher;              /* phys. address of the AXI data registers (higher 32 bit)*/
    u32 axi_data_size;                          /* AXI data register size*/

    u32 *ddr_virt_base_addr;                    /* virt. address of the DDR buffer */
    u32 *ddr_phys_base_addr;                    /* phys. address of the DDR buffer */
    u32 ddr_size;                               /* DDR buffer size */

    void __iomem *cdma_virt_base_addr;          /* virt. address of the AXI Lite CDMA control registers */

    const struct config_registers *config_regs; /* Config register struct. Currently not used.*/
    struct mutex sem;                           /* Mutex */
};

// Config register structure
struct config_registers {
    u32 CRC;
    u32 FAR;
    u32 FDRI;
    u32 FDRO;
    u32 CMD;
    u32 CTL;
    u32 MASK;
    u32 STAT;
    u32 LOUT;
    u32 COR;
    u32 MFWR;
    u32 FLR;
    u32 KEY;
    u32 CBC;
    u32 IDCODE;
    u32 AXSS;
    u32 C0R_1;
    u32 CSOB;
    u32 WBSTAR;
    u32 TIMER;
    u32 BOOTSTS;
    u32 CTL_1;
};

#endif
