#include "axi-cdma.h"

// For a detailed description of the IP core see PG034

// AXI Lite register offsets
#define XAXICDMA_CR_OFFSET             0x00000000  /* < Control register */
#define XAXICDMA_SR_OFFSET             0x00000004  /* < Status register */
#define XAXICDMA_SRCADDR_LOWER_OFFSET  0x00000018  /* < Lowe source address register */
#define XAXICDMA_SRCADDR_HIGHER_OFFSET 0x0000001C  /* < Higher source address register */
#define XAXICDMA_DSTADDR_LOWER_OFFSET  0x00000020  /* < Lower destination address register */
#define XAXICDMA_DSTADDR_HIGHER_OFFSET 0x00000024  /* < Higher destination address register */
#define XAXICDMA_BTT_OFFSET            0x00000028  /* < Bytes to transfer */

// Control register masks
#define XAXICDMA_KEY_HOLE_WRITE        0x00000020 /* < Set key hole write */
#define XAXICDMA_SIMPLE_IRQ            0x00005000 /* < Set ERR_IrqEn and IOC_IrqEn */
#define XAXICDMA_RESET                 0x00000004 /* < Reset every register */

// Status register masks
#define XAXICDMA_IDLE                  0x00000002 /* < Check Idle bit */
#define XACDMA_IOC_IRQ                 0x00001000 /* < Check IOC_Irq bit */
#define XAXICDMA_ERR_IRQ               0x00004000 /* < Check Err_Irq bit */

// Error flags
#define XACDMA_NOT_IDLE               -1
#define XACDMA_WRITE_ERROR            -2
#define XACDMA_WRITE_TIMEOUT          -3

// Additional defines
#define XACDMA_MAX_RETRIES            10000

/**
 * axi_cdma_set_interrupts - Enable the simple dma interrupts on error and complete
 * @drvdata: a pointer to the drvdata.
 **/
static inline void axi_cdma_set_interrupts(struct hbicap_drvdata *drvdata)
{
    u32 control_register;
    control_register = ioread32le(drvdata->cdma_virt_base_addr + XAXICDMA_CR_OFFSET);
    iowrite32le(control_register | XAXICDMA_SIMPLE_IRQ, drvdata->cdma_virt_base_addr + XAXICDMA_CR_OFFSET);
}

/**
 * axi_cdma_set_source_addr - Set the source address in the DDR for the data
 * @drvdata: a pointer to the drvdata.
 * @addr:    DDR source address 
 **/
static inline void axi_cdma_set_source_addr(struct hbicap_drvdata *drvdata, u32 addr_higher, u32 addr_lower)
{
    iowrite32le(addr_higher, drvdata->cdma_virt_base_addr + XAXICDMA_SRCADDR_HIGHER_OFFSET);
    iowrite32le(addr_lower , drvdata->cdma_virt_base_addr + XAXICDMA_SRCADDR_LOWER_OFFSET);
}

/**
 * axi_cdma_set_destination_addr - Set the destination address aka AXI HBICAP data port
 * @drvdata: a pointer to the drvdata.
 * @addr:    destination address 
 **/
static inline void axi_cdma_set_destination_addr(struct hbicap_drvdata *drvdata, u32 addr_higher, u32 addr_lower)
{
    iowrite32le(addr_higher, drvdata->cdma_virt_base_addr + XAXICDMA_DSTADDR_HIGHER_OFFSET);
    iowrite32le(addr_lower , drvdata->cdma_virt_base_addr + XAXICDMA_DSTADDR_LOWER_OFFSET);
}

/**
 * axi_cdma_set_length - Set the number of bytes and start the transmission
 * @drvdata: a pointer to the drvdata.
 * @length:  number of bytes to transmit
 **/
static inline void axi_cdma_set_size(struct hbicap_drvdata *drvdata, u32 size)
{
    iowrite32le(size, drvdata->cdma_virt_base_addr + XAXICDMA_BTT_OFFSET);
}

/**
 * axi_cdma_check_IDLE - Check if the IDLE bit is set
 * @drvdata: a pointer to the drvdata.
 **/
static inline u32 axi_cdma_check_IDLE(struct hbicap_drvdata *drvdata)
{
    u32 status_register;
    status_register = ioread32le(drvdata->cdma_virt_base_addr + XAXICDMA_SR_OFFSET);

    return ((status_register & XAXICDMA_IDLE) ? 1 : 0);
}

/**
 * axi_cdma_busy - Wait until the transmission is finished, check for transmission errors
 * @drvdata: a pointer to the drvdata.
 **/
static inline u32 axi_cdma_busy(struct hbicap_drvdata *drvdata)
{
    u32 status_register;
    u32 retries = 0;
    u32 status = 0;

    // wait until the transmission is complete
    do
    {
        status_register = ioread32le(drvdata->cdma_virt_base_addr + XAXICDMA_SR_OFFSET);
        retries++;
        if (retries > XACDMA_MAX_RETRIES)
        {
            status = XACDMA_WRITE_TIMEOUT;
            goto error;
        }
    }
    while(!(status_register & XACDMA_IOC_IRQ));

    // Reset IOC_IRQ flag
    iowrite32le(XACDMA_IOC_IRQ, drvdata->cdma_virt_base_addr + XAXICDMA_SR_OFFSET);

   // Check the ERR_IRQ flag
    if(status_register & XAXICDMA_ERR_IRQ)
        status = XACDMA_WRITE_ERROR;

error:
    return status;
}

/**
 * axi_cdma_reset - Reset every register of the AXI CDMA
 * @drvdata: a pointer to the drvdata.
 **/
void axi_cdma_reset(struct hbicap_drvdata *drvdata)
{
    iowrite32le(XAXICDMA_RESET, drvdata->cdma_virt_base_addr + XAXICDMA_CR_OFFSET);
}

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
                    u32 destination_addr_higher, u32 destination_addr_lower, u32 size)
{
    int status = 0;

    // Check if CDMA is idle
    if(!axi_cdma_check_IDLE(drvdata)){
        status = XACDMA_NOT_IDLE;
        goto error;
    }

    // Set CDMA interrupts
    axi_cdma_set_interrupts(drvdata);

    // Set CDMA source address
    axi_cdma_set_source_addr(drvdata, source_addr_higher, source_addr_lower);

    // Set the CDMA destination address
    axi_cdma_set_destination_addr(drvdata, destination_addr_higher, destination_addr_lower);

    // write the data to the HBICAP
    axi_cdma_set_size(drvdata, size);

    // Check if the transmission was sucessfull
    status = axi_cdma_busy(drvdata);

error:
    return status;
}
