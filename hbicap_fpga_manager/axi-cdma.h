/**
* Low level functions to access the AXI lite control registers of the AXI CDMA
* For a detailed description of the IP core see Xilinx PG034
*
* TODO: This should be in an separate driver that is called by the HBICAP FPGA manager
**/
#ifndef AXI_CDMA_H_    /* prevent circular inclusions */
#define AXI_CDMA_H_    /* by using protection macros */

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include "hbicap-fpga.h"

#define iowrite32le(v,p) ({ __iowmb(); __raw_writel((__force __u32) cpu_to_le32(v), p); })
#define ioread32le(p)    ({ __u32 __v = le32_to_cpu((__force __le32)__raw_readl(p)); __iormb(__v); __v; })

/**
 * axi_cdma_reset - Reset every register of the AXI CDMA
 * @drvdata: a pointer to the drvdata.
 **/
void axi_cdma_reset(struct hbicap_drvdata *drvdata);

/**
* axi_cdma_write - Write data from DDR to PL
* @drvdata: a pointer to the drvdata.
* @source_addr_lower: the lower 32 bits of the physical DDR base address
* @source_addr_higher: the higher 32 bits of the physical DDR base address
* @destination_addr_lower: the lower 32 bits of the AXI address in the PL
* @destination_addr_higher: the higher 32 bits of the AXI address in the PL
* @size: the size of the data to be written (in bytes)
**/
int axi_cdma_write(struct hbicap_drvdata *drvdata,  u32 source_addr_higher, u32 source_addr_lower,
                    u32 destination_addr_higher, u32 destination_addr_lower, u32 size);

#endif
