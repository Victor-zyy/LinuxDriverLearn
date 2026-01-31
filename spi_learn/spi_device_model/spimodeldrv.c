#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/gpio.h>

#include <linux/spi/spi.h>

#include <linux/uaccess.h> // for copy form user
#include <linux/delay.h> // for sleep function
#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/semaphore.h> // struct semaphore
#include <linux/proc_fs.h>  // read_procmem
#include <linux/seq_file.h> // seq_file stack

#include "spimodeldrv.h"
#include <linux/module.h>

#define USE_DEVICE_OF    0
#define USE_SPI_DEVICE   1

#define SSD1309_MAJOR   0 /* for dynamically */

int ssd1309_major = SSD1309_MAJOR;
module_param(ssd1309_major, int, S_IRUGO);

struct ssd1309_oled_dev {
    struct semaphore sem;   /* to manage mutex exclusion */
    struct spi_device *spi_dev; /* to manage the spi_dev info */
    #if USE_DEVICE_OF
    struct device_node *oled_node;
    #elif USE_SPI_DEVICE
    struct spi_board_info *board_info;
    #endif
    int oled_dc_pin; // D/C Control Gpio Pin Number data = 1, command = 0
    int oled_rst_pin; // RST Control Gpio Pin Number
    struct cdev cdev;
};

static struct ssd1309_oled_dev *ssd1309_sdev;

#if USE_DEVICE_OF

static struct device_node *waveshare_oled_device_node; 

#elif USE_SPI_DEVICE

#define SPI_BUS_NUM     2
struct dev_spi_info {
    int rst_gpio;
    int dc_gpio;
};
static struct dev_spi_info waveshare_dev_info = {
    .dc_gpio = 2,
    .rst_gpio = 8,
};

static struct spi_board_info waveshare_spi_oled_info = {
	.modalias = "zynex,ecspi_oled",
	.max_speed_hz = 200000,
	.chip_select = 0,
    .bus_num = SPI_BUS_NUM,
	.mode = SPI_MODE_3,
    .platform_data = &waveshare_dev_info,
};

#endif


static int waveshare_spi_oled_probe(struct spi_device *spi );
static int waveshare_spi_oled_remove(struct spi_device *spi );

#define DEBUGSPIMODEL

#ifdef DEBUGSPIMODEL /* Use proc filesystem if debugging */

/**
 * For now the seq_file implementation will exist in parallel. The older
 * read_procmem function should maybe go away, though.
 */

/**
 * Here are our sequence iteration methods. Our "position" is simply the
 * device number.
 */

static void *spimodel_seq_start(struct seq_file *s, loff_t *pos)
{
    if (*pos == 0) /* starting at a new pos */
    {
        return ssd1309_sdev;
    } else {
        return NULL;
    }
}

static void *spimodel_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    return NULL;
}

static void spimodel_seq_stop(struct seq_file *s, void *v)
{
    /* Actually we do have nothing here*/
}

static int spimodel_seq_show(struct seq_file *s, void *v)
{
    struct ssd1309_oled_dev *dev = (struct ssd1309_oled_dev*)v; // address convert

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    /* Only to show some spi information to the user */
    seq_printf(s, "max_speed_hz  = %d\r\n", dev->spi_dev->max_speed_hz);
    seq_printf(s, "chip_select   = %d\r\n", (int)dev->spi_dev->chip_select);
    seq_printf(s, "bits_per_word  = %d\r\n", (int)dev->spi_dev->bits_per_word);
    seq_printf(s, "mode          = %02x\r\n", dev->spi_dev->mode);
    seq_printf(s, "cs_gpio       = %d\r\n", dev->spi_dev->cs_gpio);
    seq_printf(s, "waveshare oled control pin num = %d\r\n", dev->oled_dc_pin);
    seq_printf(s, "waveshare oled reset   pin num = %d\r\n", dev->oled_rst_pin);
    up(&dev->sem);
    return 0;
}

/**
 * Tie the sequence operations up
 */
static struct seq_operations spimodel_seq_ops = {
    .start = spimodel_seq_start,
    .next = spimodel_seq_next,
    .stop = spimodel_seq_stop,
    .show = spimodel_seq_show
};

/**
 * Now to implement the /proc file we need only make on open method
 * which sets up the sequence file operations
 */
static int spimodel_proc_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &spimodel_seq_ops);
}

/**
 * Create a set of file operations for our proc file
 */
static struct file_operations scullc_proc_ops = {
    .owner      = THIS_MODULE,
    .open       = spimodel_proc_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = seq_release
};

/**
 * Actually create and remove the /proc file(s).
 */
static void spimodel_create_proc(void)
{
    struct proc_dir_entry *entry;
    /* this function has been deprecated */
    /* create_proc_entry is deprecated since kernel version3.1 now is changed to proc_create*/
    entry = proc_create("ssd1309_sdev", 0, NULL, &scullc_proc_ops); 
}

static void scullc_remove_proc(void)
{
    /* no problem if it was not registered */
    remove_proc_entry("ssd1309_sdev", NULL);
}
#endif /* DEBUGSPIMODEL */



/**
 *  Oled Layer Spi Device Specific Low-Level Function
 */
static int spi_oled_send_command(struct ssd1309_oled_dev *dev, u8 command)
{
    int error = 0;
    u8 tx_data = command;
    struct spi_message *message;
    struct spi_transfer *transfer;

    // push low of DC control pin
    gpio_direction_output(dev->oled_dc_pin, 0);

    // kzalloc space
    message  = kzalloc(sizeof(struct spi_message), GFP_KERNEL);
    transfer = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);

    // Fullfill the two structs
    transfer->tx_buf = &tx_data;
    transfer->len = 1; 
    spi_message_init(message);
    spi_message_add_tail(transfer, message);

    // Ready to Send Message
    error = spi_sync(dev->spi_dev, message);
    kfree(message);
    kfree(transfer);
    if( 0 != error ){
        pr_err("spi_sync error please check!\r\n");
    }
    // push high of DC control pin
    gpio_direction_output(dev->oled_dc_pin, 1);
    return 0;
}

static int spi_oled_send_commands(struct ssd1309_oled_dev *dev, const u8 *commands, u16 length){
    int error = 0;
    struct spi_message *message;
    struct spi_transfer *transfer;

    // push low of DC control pin
    gpio_direction_output(dev->oled_dc_pin, 0);

    // kzalloc space
    message  = kzalloc(sizeof(struct spi_message), GFP_KERNEL);
    transfer = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);

    // Fullfill the two structs
    transfer->tx_buf = commands;
    transfer->len = length; 
    spi_message_init(message);
    spi_message_add_tail(transfer, message);

    // Ready to Send Message
    error = spi_sync(dev->spi_dev, message);
    kfree(message);
    kfree(transfer);
    if( 0 != error ){
        pr_err("spi_sync error please check!\r\n");
    }
    // push high of DC control pin
    gpio_direction_output(dev->oled_dc_pin, 1);
    return 0;
}

static int spi_oled_send_data(struct ssd1309_oled_dev *dev, u8 data){

    int error = 0;
    u8 tx_data = data;
    struct spi_message *message;
    struct spi_transfer *transfer;

    // push high of DC control pin
    gpio_direction_output(dev->oled_dc_pin, 1);

    // kzalloc space
    message  = kzalloc(sizeof(struct spi_message), GFP_KERNEL);
    transfer = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);

    // Fullfill the two structs
    transfer->tx_buf = &tx_data;
    transfer->len = 1; 
    spi_message_init(message);
    spi_message_add_tail(transfer, message);

    // Ready to Send Message
    error = spi_sync(dev->spi_dev, message);
    kfree(message);
    kfree(transfer);
    if( 0 != error ){
        pr_err("spi_sync error please check!\r\n");
    }
    return 0;
}

static int spi_oled_send_datas(struct ssd1309_oled_dev *dev, u8 *data, u16 length){
   int error = 0;
   int index = 0;
   struct spi_message *message;
   struct spi_transfer *transfer; 
   gpio_direction_output(dev->oled_dc_pin, 1);
   message = kzalloc(sizeof(struct spi_device), GFP_KERNEL);
   transfer = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);

   // cycle send 
   do
   {
        if( length > 30 ){
            transfer->tx_buf = data + index;
            transfer->len = 30;
            spi_message_init(message);
            spi_message_add_tail(transfer,message);
            index += 30;
            length -= 30;
        }
        else {

            transfer->tx_buf = data + index;
            transfer->len = length;
            spi_message_init(message);
            spi_message_add_tail(transfer,message);
            index += length;
            length = 0;
        }
        error = spi_sync(dev->spi_dev, message);
        if( 0 != error ){
            pr_err("spi_sync error!\r\n");
            return -1;
        }
    /* code */
   } while (length > 0);
   
   kfree(message);
   kfree(transfer);

   return 0;
}

void spi_oled_fill(struct ssd1309_oled_dev *dev, unsigned char bmp_data){
    u8 y, x;
    for( y = 0; y < 8; y++ ){
        spi_oled_send_command(dev, 0xb0 + y);
        spi_oled_send_command(dev, 0x00);
        spi_oled_send_command(dev, 0x10);
        for( x = 0; x < 128; x++ ){
            spi_oled_send_data(dev, bmp_data);
        }
    }
}

static int spi_oled_display_buffer(struct ssd1309_oled_dev *dev, u8 *dis_buf){
    int error = 0;
    u16 page,column;
    u8 temp;
    for (page=0; page<8; page++) {
        /* write data physical reverse */
        for(column=0; column<128; column++) {
            temp = dis_buf[(7-page) + column*8]; //page = 0 column = 0; temp = Image[7];
            //temp = Image[(page) + column*8]; //page = 0 column = 0; temp = Image[7];
            error += spi_oled_send_data(dev, temp);
        }       
    }
    /* at first we set horizontal mode */
    if( 0 != error ){
        pr_err("spi_oled_display_buffer error %d\r\n", error);
        return -1;
    }
    return page * column;
}


const u8 oled_init_data[] = {0xAE, 0x00, 0x10, 
    0x20, 0x00,  0xA6, 
    0xA8, 0x3F,  0xD3, 0x00,
    0xD5, 0x80,  0xD9, 0x22, 0xA0, 0xC8,
    0xDA, 0x12, 0xDB, 0x40};

static void spi_oled_rst(struct ssd1309_oled_dev *dev)
{
    // push low of DC control pin
    gpio_direction_output(dev->oled_rst_pin, 1);
    gpio_direction_output(dev->oled_rst_pin, 0);
    msleep(100);
    gpio_direction_output(dev->oled_rst_pin, 1);

}
static void spi_oled_init(struct ssd1309_oled_dev *dev){
    spi_oled_rst(dev);
    spi_oled_send_commands(dev, oled_init_data, sizeof(oled_init_data));
    msleep(100);
    spi_oled_send_command(dev, 0xAF);
    spi_oled_fill(dev, 0x00);
}
/**
 *  WaveShare Spi Oled FileOperations List
 */
static int waveshare_spi_oled_open(struct inode *inode, struct file *filp){
    /**
     * Init WaveShare Transparent Spi-Oled
     */

    struct ssd1309_oled_dev *dev; /* device information */

    dev = container_of(inode->i_cdev, struct ssd1309_oled_dev, cdev);

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    spi_oled_init(dev);
    filp->private_data = dev;
    up(&dev->sem);
    return 0;
}

static int waveshare_spi_oled_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *off){

    int copy_number = 0;
    u8 *write_data;
    struct ssd1309_oled_dev *dev = filp->private_data;
    
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    write_data = (u8 *)kzalloc((X_WIDTH % 8 == 0 ? X_WIDTH / 8 : X_WIDTH / 8 + 1) * Y_WIDTH, GFP_KERNEL);

    copy_number = copy_from_user(write_data, buf, cnt);
    copy_number = spi_oled_display_buffer(dev, write_data); /* to simplify the display we just to reflesh the whole display 64 * 128 pixel */

    kfree(write_data);

    up(&dev->sem);

    return copy_number;
}

static int waveshare_spi_oled_release(struct inode *inode, struct file *filp){
    // close the display
    struct ssd1309_oled_dev *dev = filp->private_data;
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    spi_oled_send_command(dev, 0xAE);
    up(&dev->sem);
    return 0;
}

/**
 * Fops Function Lists
 */
static struct file_operations oled_chr_dev_fops = {
    .owner = THIS_MODULE,
    .open = waveshare_spi_oled_open,
    .write = waveshare_spi_oled_write,
    .release = waveshare_spi_oled_release,
};

/**
 *  SPI Driver and SPI Device ID Match Table
 */
static const struct spi_device_id waveshare_spi_oled_id[] = {
    {"zynex,ecspi_oled", 0},
    {}
};

/**
 *  Device Tree Match Table
 */
static const struct of_device_id waveshare_spi_oled_of[] = {
    {.compatible = "zynex,ecspi_oled"},
    {}
};

/**
 * SPI Driver
 */
static struct spi_driver waveshare_spi_oled_driver = {
    .probe = waveshare_spi_oled_probe,
    .remove = waveshare_spi_oled_remove,
    .id_table = waveshare_spi_oled_id,
    .driver = {
        .name = "waveshare_ecspi_oled",
        .owner = THIS_MODULE,
        .of_match_table = waveshare_spi_oled_of,
    },
};


static struct class *oled_class;
static struct device *oled_device;

static void ssd1309_cdev_setup(struct ssd1309_oled_dev *dev, int index)
{
    int err, devno = MKDEV(ssd1309_major, 0 + index);

    cdev_init(&dev->cdev, &oled_chr_dev_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &oled_chr_dev_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    /* create class and device file */
    oled_class = class_create(THIS_MODULE, "spioled");
    oled_device = device_create(oled_class, NULL, devno, NULL, "spioled");
    /*Failed gracefully if need be */
    if (err)
        printk(KERN_NOTICE "Error %d adding spioled%d", err, index);
}


static int waveshare_spi_oled_probe(struct spi_device *spi )
{
    int ret = -1;
    dev_t devno;
    
    #if USE_DEVICE_OF
    /**
     * 1. Get device tree node by path
     */
    ssd1309_sdev->oled_node = of_find_node_by_path("/soc/aips-bus@2000000/spba-bus@2000000/ecspi@2010000/ecspi_oled@1");
    if( NULL == ssd1309_sdev->oled_node ){
        pr_emerg("get ecspi_oled@1 failed from device tree\r\n");
    }
    /**
     * 2. Get D/C Pin Number from device node and Set as high
     */
    ssd1309_sdev->oled_dc_pin = of_get_named_gpio(ssd1309_sdev->oled_node, "dc_control_pin", 0);
    ssd1309_sdev->oled_rst_pin = of_get_named_gpio(ssd1309_sdev->oled_node, "rst_control_pin", 0);
    /**
     * 3. Initialize Spi Device, why we didn't get param from device tree
     */
    ssd1309_sdev->spi_dev = spi; // from function param
    ssd1309_sdev->spi_dev->mode = SPI_MODE_3; // CPOL = 1 CPHA = 1
    ssd1309_sdev->spi_dev->max_speed_hz = 500000; // 500Khz
    spi_setup(ssd1309_sdev->spi_dev);

    #elif USE_SPI_DEVICE
    ssd1309_sdev->oled_dc_pin  = ((struct dev_spi_info *)spi->dev.platform_data)->dc_gpio;
    ssd1309_sdev->oled_rst_pin= ((struct dev_spi_info *)spi->dev.platform_data)->rst_gpio;
    #endif
    gpio_direction_output(ssd1309_sdev->oled_dc_pin, 1);
    gpio_direction_output(ssd1309_sdev->oled_rst_pin, 1);
    /**
     * 4. Register chardev and create device file dynamically 
     */
    if (ssd1309_major) {
        devno = MKDEV(ssd1309_major, 0);
        ret = register_chrdev_region(devno, 1, "spioled");
    } else {
        ret = alloc_chrdev_region(&devno, 0, 1, "spioled");
        ssd1309_major = MAJOR(devno);
    }

    if( ret < 0 ){
        pr_emerg("failed to alloc oled_devno\r\n");
        goto alloc_err;
    }

    sema_init(&ssd1309_sdev->sem, 1);
    ssd1309_cdev_setup(ssd1309_sdev, 0);
#ifdef DEBUGSPIMODEL
    spimodel_create_proc();
#endif

    return 0;
alloc_err:
    unregister_chrdev_region(devno, 1);
    return -1;
}

static int waveshare_spi_oled_remove(struct spi_device *spi )
{
    /**
     * Resources Recycle the order is the traverse of the probe
     */
    dev_t devno = MKDEV(ssd1309_major, 0);
    device_destroy(oled_class, devno);
    class_destroy(oled_class);

    cdev_del(&ssd1309_sdev->cdev); // device number
    unregister_chrdev_region(devno, 1);
#ifdef DEBUGSPIMODEL
    scullc_remove_proc();
#endif
    return 0; 
}

/**
 * Module Init Entry
 */
static int __init spi_transparent_oled_init(void)
{
    int error;
    struct spi_master * master;
    ssd1309_sdev = kmalloc(sizeof(struct ssd1309_oled_dev), GFP_KERNEL);
    memset(ssd1309_sdev, 0, sizeof(struct ssd1309_oled_dev));
#if USE_SPI_DEVICE
    master = spi_busnum_to_master(waveshare_spi_oled_info.bus_num);
    if (master == NULL) {
        pr_err("SPI master busnum %d not fount\n", waveshare_spi_oled_info.bus_num);
        return -ENODEV;
    }
    ssd1309_sdev->spi_dev = spi_new_device(master, &waveshare_spi_oled_info);
    if (ssd1309_sdev->spi_dev == NULL ) {
        pr_err("Failed to create spi device!\n");
        return -ENODEV;
    }
    error = spi_setup(ssd1309_sdev->spi_dev);
    if (error < 0) {
        pr_err("Failed to setup device!\n");
        spi_unregister_device(ssd1309_sdev->spi_dev);
        return -ENODEV;
    }
#endif
    /* to check whether it will probe */
    error = spi_register_driver( &waveshare_spi_oled_driver );

    return error;
}

 /**
  * Module Exit Entry
  * 
  */
static void __exit spi_transparent_oled_exit(void)
{
    spi_unregister_driver(&waveshare_spi_oled_driver);
}

module_init(spi_transparent_oled_init);
module_exit(spi_transparent_oled_exit);

MODULE_AUTHOR("Zynex Victor zyy");
MODULE_LICENSE("GPL");