#ifndef HWICAP_FPGA_FIFO_H_    /* prevent circular inclusions */
#define HWICAP_FPGA_FIFO_H_    /* by using protection macros */

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include "hwicap-fpga.h"

#define iowrite32le(v,p) ({ __iowmb(); __raw_writel((__force __u32) cpu_to_le32(v), p); })
#define ioread32le(p)    ({ __u32 __v = le32_to_cpu((__force __le32)__raw_readl(p)); __iormb(__v); __v; })

/* Reads integers from the device into the storage buffer. */
int fifo_icap_get_configuration(
        struct hwicap_drvdata *drvdata,
        u32 *FrameBuffer,
        u32 NumWords);

/* Writes integers to the device from the storage buffer. */
int fifo_icap_set_configuration(
        struct hwicap_drvdata *drvdata,
        u32 *FrameBuffer,
        u32 NumWords);

u32 fifo_icap_get_status(struct hwicap_drvdata *drvdata);
void fifo_icap_reset(struct hwicap_drvdata *drvdata);
void fifo_icap_flush_fifo(struct hwicap_drvdata *drvdata);

#endif
