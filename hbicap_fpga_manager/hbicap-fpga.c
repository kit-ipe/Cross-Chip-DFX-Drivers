//TODO: remove unnecessary header
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "hbicap-fpga.h"
#include "axi-hbicap.h"
#include "axi-cdma.h"


#include <linux/dma-mapping.h>


#define DRIVER_NAME "hbicap_fpga_manager"
#define UNIMPLEMENTED 0xFFFF

/* Number of times to poll the done register. This has to be large
 * enough to allow an entire configuration to complete. If an entire
 * page (4kb) is configured at once, that could take up to 4k cycles
 * with a byte-wide icap interface. In most cases, this driver is
 * used with a much smaller fifo, but this should be sufficient in the
 * worst case.
 */
#define XHI_MAX_RETRIES     5000

// config registers are based on virtex 6 in the original driver
static const struct config_registers zynq_usp_config_registers = {
    .CRC = 0,
    .FAR = 1,
    .FDRI = 2,
    .FDRO = 3,
    .CMD = 4,
    .CTL = 5,
    .MASK = 6,
    .STAT = 7,
    .LOUT = 8,
    .COR = 9,
    .MFWR = 10,
    .FLR = UNIMPLEMENTED,
    .KEY = UNIMPLEMENTED,
    .CBC = 11,
    .IDCODE = 12,
    .AXSS = 13,
    .C0R_1 = 14,
    .CSOB = 15,
    .WBSTAR = 16,
    .TIMER = 17,
    .BOOTSTS = 22,
    .CTL_1 = 24,
};

/**
 * struct hbicap_fpga_priv - Private data structure
 * @dev:          Device data structure
 * @feature_list: Firmware supported feature list
 * @version:      Firmware version info. The higher 16 bytes belong to
 *                the major version number and the lower 16 bytes belong
 *                to a minor version number.
 * @flags:        flags which is used to identify the bitfile type
 * @size:         Size of the Bit-stream used for readback
 */
struct hbicap_fpga_priv {
    struct device *dev;
    u32 feature_list;
    u32 version;
    u32 flags;
    u32 size;
    struct hbicap_drvdata *drvdata;
};

/** function hbicap_setup - helper function to setup the HBICAP IP Core
* @dev:   device struct
* @priv:  hbicap_fpga_priv struct
* @return 0 if success
*/
static int hbicap_setup(struct device *dev, struct hbicap_fpga_priv *priv)
{
    struct resource res;
    const struct config_registers *config_regs = &zynq_usp_config_registers;

    struct hbicap_drvdata *drvdata = NULL;
    int retval = 0;
    dma_addr_t dma_handle;

    // Allocate the driver data struct
    drvdata = kzalloc(sizeof(struct hbicap_drvdata), GFP_KERNEL);
    if (!drvdata) {
        retval = -ENOMEM;
        goto failed0;
    }
    dev_set_drvdata(dev, (void *)drvdata);

    // Get the AXI Lite control register address and size
    retval = of_address_to_resource(dev->of_node, 0, &res);
    if (retval) {
        dev_err(dev, "Invalid AXI Lite address in device tree\n");
        goto failed0;
    }

    drvdata->axi_lite_phys_base_addr = res.start;
    drvdata->axi_lite_phys_end_addr  = res.end;
    drvdata->axi_lite_size           = resource_size(&res);

    // Lock the memory region for the AXI Lite control registers
    if (!request_mem_region(drvdata->axi_lite_phys_base_addr,
                    drvdata->axi_lite_size, DRIVER_NAME)) {
        dev_err(dev, "Couldn't lock memory region at %Lx\n",(unsigned long long) res.start);
        retval = -EBUSY;
        goto failed1;
    }

    // Create an virtual address space for the AXI Lite control registers
    drvdata->axi_lite_virt_base_addr = ioremap(drvdata->axi_lite_phys_base_addr, drvdata->axi_lite_size);
    if (!drvdata->axi_lite_virt_base_addr) {
        dev_err(dev, "ioremap() for AXI Lite control registers failed\n");
        retval = -ENOMEM;
        goto failed2;
    }

    // Get the AXI data register address and size
    retval = of_address_to_resource(dev->of_node, 1, &res);
    if (retval) {
        dev_err(dev, "Invalid AXI data address in device tree\n");
        goto failed3;
    }

    // This is split up in higher and lower since the AXI CDMA IP core
    // has two config registers for the destination address 
    drvdata->axi_data_phys_base_lower  = (u32) res.start;
    drvdata->axi_data_phys_base_higher = (u32) (res.start >> 32);
    drvdata->axi_data_size             = (u32) resource_size(&res);

    // Lock the memory region for the AXI data register
    if (!request_mem_region(res.start,
                    drvdata->axi_data_size, DRIVER_NAME)) {
        dev_err(dev, "Couldn't lock memory region at %Lx\n",(unsigned long long) res.start);
        retval = -EBUSY;
        goto failed4;
    }

    
    // Assign the config register struct. These are currently not needed since we only
    // write the bitstream to the ICAP and nothing else
    drvdata->config_regs = config_regs;

    mutex_init(&drvdata->sem);

    // Allocate a 4k buffer in the DDR for the DMA
    // TODO: It may be better to do this in the hbicap_fpga_ops_write_init function and
    // release the memory in the hbicap_fpga_ops_write_complete function. It may also be
    // better to allocate memory for the whole bitstream and not just a 4k chunk. In
    // addition the memory must be in the lower 2G of the PS DDR to be accessible from
    // the PL.
    drvdata->ddr_size           = 4096;
    drvdata->ddr_virt_base_addr = dma_alloc_coherent(dev, 4096, &dma_handle, __GFP_DMA);
    drvdata->ddr_phys_base_addr = (u32 *) dma_handle;

    dev_info(dev, "4k DDR buffer is at 0x%p\n", drvdata->ddr_phys_base_addr);
    dev_info(dev, "WARNING: The DDR buffer must be in the lower 2GB of the memory. ToDo: Make sure this is always the case.\n");

    dev_dbg(dev, "AXI Lite ioremap %llx to %p with size %llx\n",
        (unsigned long long) drvdata->axi_lite_phys_base_addr,
        drvdata->axi_lite_virt_base_addr,
        (unsigned long long) drvdata->axi_lite_size);


    // Hack to set the base address of the AXI CDMA AXI Lite registers
    // As previously mentioned in the header the AXI CDMA stuff should be in a
    // separate driver

    // Get the AXI data register address and size
    retval = of_address_to_resource(dev->of_node, 2, &res);
    if (retval) {
        dev_err(dev, "Invalid CDMA AXI Lite address in device tree\n");
        goto failed4;
    }

    drvdata->cdma_virt_base_addr = ioremap(res.start, resource_size(&res));
    dev_dbg(dev, "AXI CDMA virtual base address:  0x%p", drvdata->cdma_virt_base_addr);

    priv->drvdata = drvdata;
    return 0;    /* success */

failed4:
    release_mem_region(res.start, drvdata->axi_data_size);

failed3:
    iounmap(drvdata->axi_lite_virt_base_addr);

failed2:
    release_mem_region(drvdata->axi_lite_phys_base_addr, drvdata->axi_lite_size);

failed1:
    kfree(drvdata);

failed0:

    return retval;
}


/** function hbicap_fpga_ops_state - return fpga manager state
* @mgr:   fpga_manager struct
* @return fpga_mgr_states enum
*/
static enum fpga_mgr_states hbicap_fpga_ops_state(struct fpga_manager *mgr)
{
    return mgr->state;
}


/** function hbicap_fpga_ops_write_init - prepare the FPGA to receive configuration data
* @mgr:   fpga_manager struct
* @info:  fpga_image_info struct
* @buf:   contiguous buffer containing FPGA image
* @size:  size of buf
* @return 0 if success
*/
static int hbicap_fpga_ops_write_init(struct fpga_manager *mgr,
                      struct fpga_image_info *info,
                      const char *buf, size_t size)
{
    struct hbicap_fpga_priv *priv;
    int eemi_flags = 0;
    struct hbicap_drvdata *drvdata;

    mgr->state = FPGA_MGR_STATE_WRITE_INIT;

    dev_dbg(&mgr->dev, "Firmware to be written: %s\n", info->firmware_name);

    priv = mgr->priv;
    priv->flags = info->flags;
    drvdata = priv->drvdata;

    /* Update firmware flags */
    if (priv->flags & FPGA_MGR_USERKEY_ENCRYPTED_BITSTREAM)
        eemi_flags |= XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_USERKEY;
    else if (priv->flags & FPGA_MGR_ENCRYPTED_BITSTREAM)
        eemi_flags |= XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_DEVKEY;
    if (priv->flags & FPGA_MGR_DDR_MEM_AUTH_BITSTREAM)
        eemi_flags |= XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_DDR;
    else if (priv->flags & FPGA_MGR_SECURE_MEM_AUTH_BITSTREAM)
        eemi_flags |= XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_OCM;
    if (priv->flags & FPGA_MGR_PARTIAL_RECONFIG)
        eemi_flags |= XILINX_ZYNQMP_PM_FPGA_PARTIAL;

    /* Validate user flgas with firmware feature list */
    dev_dbg(&mgr->dev, "Check firmware flags...\n");
    if ((priv->feature_list & eemi_flags) != eemi_flags) {
        mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
        return -EINVAL;
    }

    // HBICAP Initialising
    dev_dbg(&mgr->dev, "Initializing HBICAP...\n");

    // Reset the HBICAP to have a defined state
    dev_dbg(&mgr->dev, "Reset...\n");
    axi_hbicap_reset(drvdata);

    // In the original HWICAP char driver at this stage a desync
    // package was send to the HWICAP followed by reading the 
    // IDCODE and sending another desync package.
    // It seams that with the ICAP3 interface on Ultrascale+
    // this is no longer necessary.

    return 0;
}


/** function hbicap_fpga_ops_write - write count bytes of configuration data to the FPGA
* @mgr:   fpga_manager struct
* @buf:   contiguous buffer containing FPGA image
* @size:  size of buf
* @return 0 if success
*/
static int hbicap_fpga_ops_write(struct fpga_manager *mgr,
                 const char *buf, size_t size)
{
    struct hbicap_fpga_priv *priv;
    struct hbicap_drvdata *drvdata;
    ssize_t written = 0;
    ssize_t left = size;
    ssize_t len;
    ssize_t status;
    u32 retries = 0;

    mgr->state = FPGA_MGR_STATE_WRITE;

    priv = mgr->priv;
    drvdata = priv->drvdata;

    status = mutex_lock_interruptible(&drvdata->sem);
    if (status) {
        mgr->state = FPGA_MGR_STATE_WRITE_ERR;
        goto error;
    }

    // Write the number of 32 bit words of the bitstream to the AXI HBICAP
    axi_hbicap_set_size_register(drvdata, size >> 2);

    // Write the bitstream in chunks of 4k to the AXI HBICAP
    while (left > 0) {
        len = ((left < 4096) ? left : 4096);

        // Copy from buf to DDR
        memcpy(drvdata->ddr_virt_base_addr, buf + written, len);

        // Write the data to the AXI HBICAP via the AXI CDMA
        status = axi_cdma_write(drvdata, 0, (u32) drvdata->ddr_phys_base_addr,
            drvdata->axi_data_phys_base_higher, drvdata->axi_data_phys_base_lower, len);

        // Check if the transmission was sucessfull
        if(status) {
            dev_err(&mgr->dev, "CDMA transmission was not successfull\n");
            goto error;
        }

        // update written and left counter
        written += len;
        left -= len;
    }

    // Wait until the write has finished.
    // This checks if the number of 32 bit words specified with the size register are received
    // or if some transmissions are still outstanding.
        while (axi_hbicap_busy(drvdata)) {
            retries++;
            if (retries > XHI_MAX_RETRIES) {
                return -42;
            }
        }

    //check if the whole bitstream was written
    status = (size - written);

 error:
    mutex_unlock(&drvdata->sem);

    return status;
}


/** function hbicap_fpga_ops_write_complete - set FPGA to operating state after writing is done
* @mgr:   fpga_manager struct
* @info:  fpga_image_info struct
* @return 0 if success
*/
int hbicap_fpga_ops_write_complete(struct fpga_manager *mgr, struct fpga_image_info *info)
{
    // mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE;
	mgr->state = FPGA_MGR_STATE_OPERATING;
    return 0;
}


/**
* struct hbicap_fpga_ops - ops for low level fpga manager drivers
* @write_init:     prepare the FPGA to receive configuration data
* @write:          write count bytes of configuration data to the FPGA
* @write_complete: set FPGA to operating state after writing is done
* @state:          returns an enum value of the FPGA's state
*/
static const struct fpga_manager_ops hbicap_fpga_ops = {
    .write_init     = hbicap_fpga_ops_write_init,
    .write          = hbicap_fpga_ops_write,
    .write_complete = hbicap_fpga_ops_write_complete,
    .state          = hbicap_fpga_ops_state,
};


/** function hbicap_fpga_probe - probe function
* @pdev:  platform_device struct
* @return 0 if success
*/
static int hbicap_fpga_probe(struct platform_device *pdev)
{
    struct device *dev;
    struct hbicap_fpga_priv *priv;
    struct fpga_manager *mgr;
    int ret;

    dev = &pdev->dev;

    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    // Only support partial reconfiguration
    priv->feature_list = FPGA_MGR_PARTIAL_RECONFIG;

    // HBICAP Setup
    ret = hbicap_setup(&pdev->dev, priv);
    if (ret) {
        dev_err(&pdev->dev, "Error in hbicap_setup\n");
        return ret;
    }

    mgr = devm_fpga_mgr_create(dev, "Xilinx HBICAP FPGA Manager",
                    &hbicap_fpga_ops, priv);

    if (IS_ERR(mgr))
        return PTR_ERR(mgr);

    mgr->state = FPGA_MGR_STATE_OPERATING;

    return devm_fpga_mgr_register(dev, mgr);
}


#ifdef CONFIG_OF
/* Match table for device tree binding */
static const struct of_device_id hbicap_fpga_of_match[] = {
    { .compatible = "xlnx,hbicap-fpga", },
    {},
};
MODULE_DEVICE_TABLE(of, hbicap_fpga_of_match);
#endif

static struct platform_driver hbicap_fpga_driver = {
    .probe = hbicap_fpga_probe,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = of_match_ptr(hbicap_fpga_of_match),
    },
};


module_platform_driver(hbicap_fpga_driver);

MODULE_AUTHOR("KIT-IPE, Hendrik Krause <Hendrik.Krause@kit.edu>");
MODULE_DESCRIPTION("Xilinx HBICAP FPGA Manager");
MODULE_LICENSE("GPL");
