#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <asm/mach/map.h>
#include <asm/io.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/semaphore.h>

struct of_chrdev {
    struct cdev cdev;
    struct semaphore sem;
};

static struct of_chrdev *ofdev;

int of_chrdrv_major = 0;
module_param(of_chrdrv_major, int, S_IRUGO);

static int of_chrdrv_open(struct inode * node, struct file *flip)
{
    struct of_chrdev *dev = container_of(node->i_cdev, struct of_chrdev, cdev);
    if (down_interruptible(&dev->sem)) {
        return -ERESTARTSYS;
    }
    flip->private_data = dev;
    up(&dev->sem);
    return 0;
}
static int of_chrdrv_release (struct inode *inode, struct file *flip)
{
    return 0;
}
static ssize_t of_chrdrv_read (struct file *filp, char __user * buf, size_t count, loff_t *off){

    int i, cnt;
    u32 *val;
    struct device_node *ecspi3_node;
    struct device_node *ecspi3_par;
    struct device_node *ecspi3_child;
    struct device_node *ecspi3_sibling;
    struct device_node *p_node;

    struct property *prop;

    struct of_chrdev *dev = filp->private_data;
    if (down_interruptible(&dev->sem)) {
        return -ERESTARTSYS;
    }

    ecspi3_node = of_find_node_by_path("/soc/aips-bus@2000000/spba-bus@2000000/ecspi@2010000");
    pr_err("ecspi3_node name : %s\n", ecspi3_node->name);
    pr_err("ecspi3_node type : %s\n", ecspi3_node->type);
    pr_err("ecspi3_node phandle: %d\n", ecspi3_node->phandle);
    pr_err("ecspi3_node full_name : %s\n", ecspi3_node->full_name);
    /* find by phandle */
    p_node = of_find_node_by_phandle(ecspi3_node->phandle);
    if (p_node != NULL){
        pr_err("p_node name : %s\n", p_node->name);
        pr_err("p_node type : %s\n", p_node->type);
        pr_err("p_node phandle: %d\n", p_node->phandle);
        pr_err("p_node full_name : %s\n", p_node->full_name);
    }

    if (ecspi3_node->parent) {
        ecspi3_par = ecspi3_node->parent; 
        pr_err("ecspi3_par name : %s\n", ecspi3_par->name);
        pr_err("ecspi3_par type : %s\n", ecspi3_par->type);
        pr_err("ecspi3_par phandle: %d\n", ecspi3_par->phandle);
        pr_err("ecspi3_par full_name : %s\n", ecspi3_par->full_name);
    }
    if (ecspi3_node->child) {
        ecspi3_child = ecspi3_node->child; 
        pr_err("ecspi3_child name : %s\n", ecspi3_child->name);
        pr_err("ecspi3_child type : %s\n", ecspi3_child->type);
        pr_err("ecspi3_child phandle: %d\n", ecspi3_child->phandle);
        pr_err("ecspi3_child full_name : %s\n", ecspi3_child->full_name);
    }
    if (ecspi3_node->sibling) {
        ecspi3_sibling = ecspi3_node->sibling; 
        pr_err("ecspi3_sibling name : %s\n", ecspi3_sibling->name);
        pr_err("ecspi3_sibling type : %s\n", ecspi3_sibling->type);
        pr_err("ecspi3_sibling phandle: %d\n", ecspi3_sibling->phandle);
        pr_err("ecspi3_sibling full_name : %s\n", ecspi3_sibling->full_name);
    }

    /* properties related attributes */
    if (ecspi3_node->child) {
        ecspi3_child = ecspi3_node->child; 
    for_each_property_of_node(ecspi3_child, prop) {
        pr_err("property : %s len : %d\n", prop->name, prop->length);
        //if (strlen(prop->name) >= strlen("phandle") && strncmp("phandle", prop->name, strlen("phandle")) == 0) {
            if (prop->length % 4 == 0) {
                val = (u32 *)prop->value;
                cnt = prop->length / 4;
                for (i = 0; i < cnt; i++) {
                    pr_err(" val[%d] = %d\n", i, be32_to_cpu(val[i]));
                }
            }
        //}
    }

	pr_info("of_property_read_bool-zynex,segment-no-remap:%d\n", of_property_read_bool(ecspi3_child, "zynex,segment-no-remap"));
	pr_info("of_property_read_bool-zynex,com-seq:%d\n",of_property_read_bool(ecspi3_child, "zynex,com-seq"));
	pr_info("of_property_read_bool-zynex,com-lrremap:%d\n",of_property_read_bool(ecspi3_child, "zynex,com-lrremap"));
	pr_info("of_property_read_bool-zynex,con-invdir:%d\n",of_property_read_bool(ecspi3_child, "zynex,com-invdir"));
	pr_info("of_property_read_bool-zynex,noseg:%d\n",of_property_read_bool(ecspi3_child, "zynex,com-noseg"));
    }

    p_node = of_find_node_by_phandle(38);
    if (p_node != NULL){
        pr_err("p_node name : %s\n", p_node->name);
        pr_err("p_node type : %s\n", p_node->type);
        pr_err("p_node phandle: %d\n", p_node->phandle);
        pr_err("p_node full_name : %s\n", p_node->full_name);
    }

    up(&dev->sem);
    return 0;
}
static ssize_t of_chrdrv_write (struct file *filp, const char __user *buf, size_t count, loff_t *off){
    struct of_chrdev *dev = filp->private_data;
    if (down_interruptible(&dev->sem)) {
        return -ERESTARTSYS;
    }

    up(&dev->sem);

    return 0;
}

static struct file_operations of_chrdrv_fops = {
    .owner = THIS_MODULE,
    .open  = of_chrdrv_open,
    .release = of_chrdrv_release,
    .read  = of_chrdrv_read,
    .write = of_chrdrv_write,
};

static struct class *of_class;
static struct device *of_device;

static void of_chrdev_setup(struct of_chrdev *dev, int index)
{
    int err, devno = MKDEV(of_chrdrv_major, index);
    cdev_init(&dev->cdev, &of_chrdrv_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops   = &of_chrdrv_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    /* create class and device file */
    of_class  = class_create(THIS_MODULE, "ofdev");
    of_device = device_create(of_class, NULL, devno, NULL, "ofdev");
    /*Failed gracefully if need be */
    if (err)
        printk(KERN_NOTICE "Error %d adding ofdev%d", err, index);
}

static int __init of_chrdrv_init(void)
{
    int devno;
    int ret;
    if (of_chrdrv_major) {
        devno = MKDEV(of_chrdrv_major, 0);
        ret = register_chrdev_region(devno, 1, "ofdev");
    } else {
        ret = alloc_chrdev_region(&devno, 0, 1, "ofdev");
        of_chrdrv_major = MAJOR(devno);
    }

    if (ret < 0) {
        pr_err("allocate and register devno error!\n");
        return -ENODEV;
    }
    
    ofdev = kmalloc(sizeof(struct of_chrdev), GFP_KERNEL);
    memset(ofdev, 0, sizeof(struct of_chrdev));

    sema_init(&ofdev->sem, 1);
    of_chrdev_setup(ofdev, 0);

    return 0;
}

static void __exit of_chrdrv_exit(void)
{
    dev_t devno = MKDEV(of_chrdrv_major, 0);
    device_unregister(of_device);
    class_unregister(of_class);
    cdev_del(&ofdev->cdev);
    unregister_chrdev_region(devno, 1);
    kfree(ofdev);
}

module_init(of_chrdrv_init);
module_exit(of_chrdrv_exit);

MODULE_AUTHOR("zynex");
MODULE_LICENSE("GPL");

