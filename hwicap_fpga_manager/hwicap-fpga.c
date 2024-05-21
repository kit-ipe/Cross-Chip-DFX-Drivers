/*
* This is my atend to rewrite the Xilinx HWICAP driver to an FPGA manager to be used with the FPGA Configuration Framework
* https://www.kernel.org/doc/html/latest/driver-api/fpga/fpga-mgr.html#c.fpga_manager_ops
* https://github.com/Xilinx/linux-xlnx/blob/master/Documentation/devicetree/bindings/fpga/fpga-region.txt
* https://events.static.linuxfound.org/sites/events/files/slides/FPGAs-under-Linux-Alan-Tull-v1.00.pdf
+ https://github.com/Xilinx/linux-xlnx/blob/master/Documentation/devicetree/configfs-overlays.txt
*
* A remove function is not needed with devm_fpga_mgr_register (https://www.kernel.org/doc/html/latest/driver-api/fpga/fpga-mgr.html#c.devm_fpga_mgr_register)
*/

//TODO: remove unnecessary header
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/slab.h>

#include "hwicap-fpga.h"
#include "hwicap-fpga-fifo.h"

#define DRIVER_NAME "hwicap_fpga_manager"
#define UNIMPLEMENTED 0xFFFF


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


/** struct hwicap_driver_config - Functions to interact with the HWICAP IP Core
* @get_configuration: getter function for the HWICAP configuration
* @set_configuration: setter function for the HWICAP configuration
* @get_status:        getter function for the HWICAP status
* @reset:             reset the HWICAP
*/
static struct hwicap_driver_config fifo_icap_config = {
    .get_configuration = fifo_icap_get_configuration,
    .set_configuration = fifo_icap_set_configuration,
    .get_status = fifo_icap_get_status,
    .reset = fifo_icap_reset,
};


/**
 * struct hwicap_fpga_priv - Private data structure
 * @dev:          Device data structure
 * @feature_list: Firmware supported feature list
 * @version:      Firmware version info. The higher 16 bytes belong to
 *                the major version number and the lower 16 bytes belong
 *                to a minor version number.
 * @flags:        flags which is used to identify the bitfile type
 * @size:         Size of the Bit-stream used for readback
 */
struct hwicap_fpga_priv {
    struct device *dev;
    u32 feature_list;
    u32 version;
    u32 flags;
    u32 size;
    struct hwicap_drvdata *drvdata;
};


/**
 * hwicap_command_desync - Send a DESYNC command to the ICAP port.
 * @drvdata: a pointer to the drvdata.
 *
 * Returns: '0' on success and failure value on error
 *
 * This command desynchronizes the ICAP After this command, a
 * bitstream containing a NULL packet, followed by a SYNCH packet is
 * required before the ICAP will recognize commands.
 */
static int hwicap_command_desync(struct hwicap_drvdata *drvdata)
{
    u32 buffer[4];
    u32 index = 0;

    /*
     * Create the data to be written to the ICAP.
     */
    buffer[index++] = hwicap_type_1_write(drvdata->config_regs->CMD) | 1;
    buffer[index++] = XHI_CMD_DESYNCH;
    buffer[index++] = XHI_NOOP_PACKET;
    buffer[index++] = XHI_NOOP_PACKET;

    /*
     * Write the data to the FIFO and intiate the transfer of data present
     * in the FIFO to the ICAP device.
     */
    return drvdata->config->set_configuration(drvdata,
            &buffer[0], index);
}


/**
 * hwicap_get_configuration_register - Query a configuration register.
 * @drvdata: a pointer to the drvdata.
 * @reg: a constant which represents the configuration
 * register value to be returned.
 * Examples: XHI_IDCODE, XHI_FLR.
 * @reg_data: returns the value of the register.
 *
 * Returns: '0' on success and failure value on error
 *
 * Sends a query packet to the ICAP and then receives the response.
 * The icap is left in Synched state.
 */
static int hwicap_get_configuration_register(struct hwicap_drvdata *drvdata,
        u32 reg, u32 *reg_data)
{
    int status;
    u32 buffer[6];
    u32 index = 0;

    /*
     * Create the data to be written to the ICAP.
     */
    buffer[index++] = XHI_DUMMY_PACKET;
    buffer[index++] = XHI_NOOP_PACKET;
    buffer[index++] = XHI_SYNC_PACKET;
    buffer[index++] = XHI_NOOP_PACKET;
    buffer[index++] = XHI_NOOP_PACKET;

    /*
     * Write the data to the FIFO and initiate the transfer of data present
     * in the FIFO to the ICAP device.
     */
    status = drvdata->config->set_configuration(drvdata,
                            &buffer[0], index);
    if (status)
        return status;


    index = 0;
    buffer[index++] = hwicap_type_1_read(reg) | 1;
    buffer[index++] = XHI_NOOP_PACKET;
    buffer[index++] = XHI_NOOP_PACKET;

    /*
     * Write the data to the FIFO and intiate the transfer of data present
     * in the FIFO to the ICAP device.
     */
    status = drvdata->config->set_configuration(drvdata,
            &buffer[0], index);
    if (status)
        return status;

    /*
     * Read the configuration register
     */
    status = drvdata->config->get_configuration(drvdata, reg_data, 1);
    if (status)
        return status;

    return 0;
}


/** function hwicap_setup - helper function to setup the HWICAP IP Core
* @dev:   device struct
* @priv:  hwicap_fpga_priv struct
* @return 0 if success
*/
static int hwicap_setup(struct device *dev, struct hwicap_fpga_priv *priv)
{
    struct resource res;
    int rc;
    const struct config_registers *config_regs = &zynq_usp_config_registers;
    const struct hwicap_driver_config *config = &fifo_icap_config;

    struct hwicap_drvdata *drvdata = NULL;
    int retval = 0;

    rc = of_address_to_resource(dev->of_node, 0, &res);
    if (rc) {
        dev_err(dev, "Invalid address in device tree\n");
        return rc;
    }

    drvdata = kzalloc(sizeof(struct hwicap_drvdata), GFP_KERNEL);
    if (!drvdata) {
        retval = -ENOMEM;
        goto failed0;
    }
    dev_set_drvdata(dev, (void *)drvdata);

    drvdata->mem_start = res.start;
    drvdata->mem_end = res.end;
    drvdata->mem_size = resource_size(&res);

    if (!request_mem_region(drvdata->mem_start,
                    drvdata->mem_size, DRIVER_NAME)) {
        dev_err(dev, "Couldn't lock memory region at %Lx\n",
            (unsigned long long) res.start);
        retval = -EBUSY;
        goto failed1;
    }

    dev_dbg(dev, "ioremap %llx to %p with size %llx\n",
                    (unsigned long long) drvdata->mem_start,
                    drvdata->base_address,
                    (unsigned long long) drvdata->mem_size);

    drvdata->base_address = ioremap(drvdata->mem_start, drvdata->mem_size);
    if (!drvdata->base_address) {
        dev_err(dev, "ioremap() failed\n");
        retval = -ENOMEM;
        goto failed2;
    }

    drvdata->config = config;
    drvdata->config_regs = config_regs;

    mutex_init(&drvdata->sem);

    priv->drvdata = drvdata;
    return 0;    /* success */

 failed2:
    iounmap(drvdata->base_address);

 failed1:
    release_mem_region(res.start, drvdata->mem_size);

 failed0:
    kfree(drvdata);

    return retval;
}


/** function hwicap_fpga_ops_state - return fpga manager state
* @mgr:   fpga_manager struct
* @return fpga_mgr_states enum
*/
static enum fpga_mgr_states hwicap_fpga_ops_state(struct fpga_manager *mgr)
{
    return mgr->state;
}


/** function hwicap_fpga_ops_write_init - prepare the FPGA to receive configuration data
* @mgr:   fpga_manager struct
* @info:  fpga_image_info struct
* @buf:   contiguous buffer containing FPGA image
* @size:  size of buf
* @return 0 if success
*/
static int hwicap_fpga_ops_write_init(struct fpga_manager *mgr,
                      struct fpga_image_info *info,
                      const char *buf, size_t size)
{
    struct hwicap_fpga_priv *priv;
    int eemi_flags = 0;
    int status;
    u32 idcode;
    struct hwicap_drvdata *drvdata;

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

    /* Validate user flags with firmware feature list */
    dev_dbg(&mgr->dev, "Check firmware flags...\n");
    if ((priv->feature_list & eemi_flags) != eemi_flags) {
        mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
        return -EINVAL;
    }

    // HWICAP Initialising
    dev_dbg(&mgr->dev, "Initializing HWICAP...\n");

    /* Abort any current transaction, to make sure we have the
     * ICAP in a good state.
     */
    dev_dbg(&mgr->dev, "Reset...\n");
    drvdata->config->reset(drvdata);

    dev_dbg(&mgr->dev, "Desync...\n");
    status = hwicap_command_desync(drvdata);
    if (status) {
        mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
        return status;
    }

    /* Attempt to read the IDCODE from ICAP.  This
     * may not be returned correctly, due to the design of the
     * hardware.
     */
    dev_dbg(&mgr->dev, "Reading IDCODE...\n");
    status = hwicap_get_configuration_register(
            drvdata, drvdata->config_regs->IDCODE, &idcode);
    if (status) {
        mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
        return status;
    }
    dev_dbg(&mgr->dev, "IDCODE = %x\n", idcode);

    dev_dbg(&mgr->dev, "Desync...\n");
    status = hwicap_command_desync(drvdata);
    if (status) {
        mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
        return status;
    }

    return 0;
}


/** function hwicap_fpga_ops_write - write count bytes of configuration data to the FPGA
* @mgr:   fpga_manager struct
* @buf:   contiguous buffer containing FPGA image
* @size:  size of buf
* @return 0 if success
*/
static int hwicap_fpga_ops_write(struct fpga_manager *mgr,
                 const char *buf, size_t size)
{
    struct hwicap_fpga_priv *priv;
    struct hwicap_drvdata *drvdata;
    ssize_t written = 0;
    ssize_t left;
    u32 *kbuf;
    ssize_t len;
    ssize_t status;

    mgr->state = FPGA_MGR_STATE_WRITE;

    priv = mgr->priv;
    drvdata = priv->drvdata;

    status = mutex_lock_interruptible(&drvdata->sem);
    if (status) {
        mgr->state = FPGA_MGR_STATE_WRITE_ERR;
        goto error;
    }

    left = size;
    left += drvdata->write_buffer_in_use;

    /* Only write multiples of 4 bytes. */
    if (left < 4) {
        status = -EINVAL;
        mgr->state = FPGA_MGR_STATE_WRITE_ERR;
        goto error;
    }

    kbuf = (u32 *) __get_free_page(GFP_KERNEL);
    if (!kbuf) {
        status = -ENOMEM;
        mgr->state = FPGA_MGR_STATE_WRITE_ERR;
        goto error;
    }

    while (left > 3) {
        /* only write multiples of 4 bytes, so there might */
        /* be as many as 3 bytes left (at the end). */
        len = left;

        if (len > PAGE_SIZE)
            len = PAGE_SIZE;
        len &= ~3;

        if (drvdata->write_buffer_in_use) {
            memcpy(kbuf, drvdata->write_buffer, drvdata->write_buffer_in_use);
            memcpy((((char *)kbuf) + drvdata->write_buffer_in_use),
                    buf + written, len - (drvdata->write_buffer_in_use));
        } else {
            memcpy(kbuf, buf + written, len);
        }

        status = drvdata->config->set_configuration(drvdata, kbuf, len >> 2);

        if (status) {
            free_page((unsigned long)kbuf);
            status = -EFAULT;
            mgr->state = FPGA_MGR_STATE_WRITE_ERR;
            goto error;
        }
        if (drvdata->write_buffer_in_use) {
            len -= drvdata->write_buffer_in_use;
            left -= drvdata->write_buffer_in_use;
            drvdata->write_buffer_in_use = 0;
        }
        written += len;
        left -= len;
    }

    // not sure if this really works as intended...
    if ((left > 0) && (left < 4)) {
        if (memcpy(drvdata->write_buffer,
                        buf + written, left)) {
            drvdata->write_buffer_in_use = left;
            written += left;
            left = 0;
        }
    }

    free_page((unsigned long)kbuf);

    //check if the whole bitstream was written
    status = (size - written);

 error:
    mutex_unlock(&drvdata->sem);

    return status;
}


/** function hwicap_fpga_ops_write_complete - set FPGA to operating state after writing is done
* @mgr:   fpga_manager struct
* @info:  fpga_image_info struct
* @return 0 if success
*/
int hwicap_fpga_ops_write_complete(struct fpga_manager *mgr, struct fpga_image_info *info)
{
    // mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE;
    mgr->state = FPGA_MGR_STATE_OPERATING;
    return 0;
}


/**
* struct hwicap_fpga_ops - ops for low level fpga manager drivers
* @write_init:     prepare the FPGA to receive configuration data
* @write:          write count bytes of configuration data to the FPGA
* @write_complete: set FPGA to operating state after writing is done
* @state:          returns an enum value of the FPGA's state
*/
static const struct fpga_manager_ops hwicap_fpga_ops = {
    .write_init = hwicap_fpga_ops_write_init,
    .write = hwicap_fpga_ops_write,
    .write_complete = hwicap_fpga_ops_write_complete,
    .state = hwicap_fpga_ops_state,
};


/** function hwicap_fpga_probe - probe function
* @pdev:  platform_device struct
* @return 0 if success
*/
static int hwicap_fpga_probe(struct platform_device *pdev)
{
    struct device *dev;
    struct hwicap_fpga_priv *priv;
    struct fpga_manager *mgr;
    int ret;

    dev = &pdev->dev;

    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    // Only support partial reconfiguration
    priv->feature_list = FPGA_MGR_PARTIAL_RECONFIG;

    // HWICAP Setup
    ret = hwicap_setup(&pdev->dev, priv);
    if (ret) {
        dev_err(&pdev->dev, "Error in hwicap_setup\n");
        return ret;
    }

    mgr = devm_fpga_mgr_create(dev, "Xilinx HWICAP FPGA Manager",
                                &hwicap_fpga_ops, priv);

    if (IS_ERR(mgr))
        return PTR_ERR(mgr);

    mgr->state = FPGA_MGR_STATE_OPERATING;

    return devm_fpga_mgr_register(dev, mgr);
}


#ifdef CONFIG_OF
/* Match table for device tree binding */
static const struct of_device_id hwicap_fpga_of_match[] = {
    { .compatible = "xlnx,hwicap-fpga", },
    {},
};
MODULE_DEVICE_TABLE(of, hwicap_fpga_of_match);
#endif

static struct platform_driver hwicap_fpga_driver = {
    .probe = hwicap_fpga_probe,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = of_match_ptr(hwicap_fpga_of_match),
    },
};


module_platform_driver(hwicap_fpga_driver);

MODULE_AUTHOR("Karlsruhe Institue of Technology (KIT) - Institute for Data Processing and Electronics (IPE)");
MODULE_DESCRIPTION("Xilinx HWICAP FPGA Manager");
MODULE_LICENSE("GPL");
