/**
* Low level functions to access the AXI lite control registers of the AXI HBICAP
* For a detailed description of the IP core see Xilinx PG349
**/
#ifndef AXI_HBICAP_H_    /* prevent circular inclusions */
#define AXI_HBICAP_H_    /* by using protection macros */

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include "hbicap-fpga.h"

#define iowrite32le(v,p) ({ __iowmb(); __raw_writel((__force __u32) cpu_to_le32(v), p); })
#define ioread32le(p)    ({ __u32 __v = le32_to_cpu((__force __le32)__raw_readl(p)); __iormb(__v); __v; })

/**
 * axi_hbicap_reset - Reset the logic of the HBICAP
 * @drvdata: a pointer to the drvdata.
 *
 * This function forces the software reset of the complete HBICAP device.
 * All the registers will return to the default value and the FIFO is also
 * flushed as a part of this software reset.
 **/
void axi_hbicap_reset(struct hbicap_drvdata *drvdata);

/**
 * axi_hbicap_busy - Return true if the ICAP is still processing a transaction.
 * @drvdata: a pointer to the drvdata.
 **/
u32 axi_hbicap_busy(struct hbicap_drvdata *drvdata);

/**
 * axi_hbicap_set_size_register - Set the the size register (number
 * of 32 bit transmission words)
 * @drvdata: a pointer to the drvdata.
 * @data: the size of the following read transaction, in words.
 **/
void axi_hbicap_set_size_register(struct hbicap_drvdata *drvdata, u32 data);

#endif
