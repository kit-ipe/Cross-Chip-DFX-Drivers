#include "axi-hbicap.h"

// AXI Lite register offsets
#define XHI_GIER_OFFSET   0x1C  /* Device Global Interrupt Enable Reg */
#define XHI_IPISR_OFFSET  0x20  /* Interrupt Status Register */
#define XHI_IPIER_OFFSET  0x28  /* Interrupt Enable Register */
#define XHI_SZ_OFFSET    0x108 /* Size Register */
#define XHI_CR_OFFSET    0x10C /* Control Register */
#define XHI_SR_OFFSET    0x110 /* Status Register */
#define XHI_WFV_OFFSET   0x114 /* Write FIFO Vacancy Register */
#define XHI_RFO_OFFSET   0x118 /* Read FIFO Occupancy Register */
#define XHI_AS_OFFSET    0x11C /* Abort Status Register */

// Device Global Interrupt Enable Register (GIER) bit masks
#define XHI_GIER_GIE_MASK 0x80000000 /* Global Interrupt enable Mask */

/**
 * HBICAP Device Interrupt Status/Enable Registers
 *
 * Interrupt Status Register (IPISR) : This register holds the
 * interrupt status flags for the device. These bits are toggle on
 * write.
 *
 * Interrupt Enable Register (IPIER) : This register is used to enable
 * interrupt sources for the device.
 * Writing a '1' to a bit enables the corresponding interrupt.
 * Writing a '0' to a bit disables the corresponding interrupt.
 *
 * IPISR/IPIER registers have the same bit definitions and are only defined
 * once.
 */
#define XHI_IPIXR_RFULL_MASK  0x00000008 /* Read FIFO Full */
#define XHI_IPIXR_WEMPTY_MASK 0x00000004 /* Write FIFO Empty */
#define XHI_IPIXR_RDP_MASK    0x00000002 /* Read FIFO half full */
#define XHI_IPIXR_WRP_MASK    0x00000001 /* Write FIFO half full */
#define XHI_IPIXR_ALL_MASK    0x0000000F /* Mask of all interrupts */

// Control register (CR) masks
#define XHI_CR_READ_DELAY_MASK 0x00000400 /* Additional Read Delay Enable Mask */
#define XHI_CR_LOCK_MASK       0x00000020 /* Lock Bit Mask */
#define XHI_CR_ABORT_MASK      0x00000010 /* Abort Bit Mask */
#define XHI_CR_SW_RESET_MASK   0x00000008 /* SW Reset Mask */
#define XHI_CR_FIFO_CLR_MASK   0x00000004 /* FIFO Clear Mask */
#define XHI_CR_READ_MASK       0x00000002 /* Read from ICAP to FIFO */
#define XHI_CR_WRITE_MASK      0x00000000 /* Write from FIFO to ICAP */ /* MODIFIED */

/* Status register (SR) masks */
#define XHI_SR_EOS_BIT_MASK    0x00000004 /* EOS Bit Mask */
#define XHI_SR_DONE_MASK       0x00000001 /* Done bit Mask  */

/**
 * axi_hbicap_set_size_register - Set the the size register (number
 * of 32 bit transmission words)
 * @drvdata: a pointer to the drvdata.
 * @data: the size of the following read transaction, in words.
 **/
void axi_hbicap_set_size_register(struct hbicap_drvdata *drvdata, u32 data)
{
    iowrite32le(data, drvdata->axi_lite_virt_base_addr + XHI_SZ_OFFSET);
}

/**
 * axi_hbicap_busy - Return true if the ICAP is still processing a transaction.
 * @drvdata: a pointer to the drvdata.
 **/
u32 axi_hbicap_busy(struct hbicap_drvdata *drvdata)
{
    u32 status = ioread32le(drvdata->axi_lite_virt_base_addr + XHI_SR_OFFSET);
    return (status & XHI_SR_DONE_MASK) ? 0 : 1;
}

/**
 * axi_hbicap_reset - Reset the logic of the HBICAP
 * @drvdata: a pointer to the drvdata.
 *
 * This function forces the software reset of the complete HBICAP device.
 * All the registers will return to the default value and the FIFO is also
 * flushed as a part of this software reset.
 **/
void axi_hbicap_reset(struct hbicap_drvdata *drvdata)
{
    u32 reg_data;
    /*
     * Reset the device by setting/clearing the RESET bit in the
     * Control Register.
     */
    reg_data = ioread32le(drvdata->axi_lite_virt_base_addr + XHI_CR_OFFSET);

    iowrite32le(reg_data | XHI_CR_SW_RESET_MASK,
                drvdata->axi_lite_virt_base_addr + XHI_CR_OFFSET);

    iowrite32le(reg_data & (~XHI_CR_SW_RESET_MASK),
                drvdata->axi_lite_virt_base_addr + XHI_CR_OFFSET);
}

/**
 * axi_hbicap_write_fifo_vacancy - Query the write fifo available space.
 * @drvdata: a pointer to the drvdata.
 *
 * Return the number of words that can be safely pushed into the write fifo.
 **/
static inline u32 axi_hbicap_write_fifo_vacancy(
        struct hbicap_drvdata *drvdata)
{
    return ioread32le(drvdata->axi_lite_virt_base_addr + XHI_WFV_OFFSET);
}
