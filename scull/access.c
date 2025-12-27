/**
 *  access.c -- the files with access control on open
 */

 /* cloned devices as a use for kobjects */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fcntl.h>
#include <linux/tty.h> //
#include <asm/atomic.h> //atomic
#include <linux/list.h>
#include <linux/errno.h>

#include "scull.h" /* local definitions */


static dev_t scull_a_firstdev; /* where our range begins */

/**
 *  These devices fall back on the main scull operations. They
 *  only differ in the implementation of open() and close()
 */

/*********************************
 * 
 *  The first device is the single-open one,
 *  it has an hw structure and an open count
 */

static struct scull_dev scull_s_device;
static atomic_t scull_s_available = ATOMIC_INIT(1);

static int scull_s_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev = &scull_s_device; /* device information */

    /* Atomically decrements @v by 1 and
    * returns true if the result is 0, or false for all other
    * cases.
    * */
    if (!atomic_dec_and_test(&scull_s_available)) {
        atomic_inc(&scull_s_available);
        return -EBUSY; /* already open */
    }

    /* Then everything else is copied from the bare scull device */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_trim(dev);
    filp->private_data = dev;

    return 0; /* indicate success */
}

static int scull_s_release(struct inode *inode, struct file* filp)
{
    atomic_inc(&scull_s_available);
    return 0;
}
/**
 *  The other operations for the single-open device come from the bare devices
 */
struct file_operations scull_sngl_fops = {
    .owner  =   THIS_MODULE,
    .llseek =   scull_llseek,
    .read   =   scull_read,
    .write  =   scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open   =   scull_s_open,
    .release =  scull_s_release
};


/****************************
 *  Next, the "uid" device, it can be opened multiple times by the same user
 *  but access is denied to other users if the device is open
 */
static struct scull_dev scull_u_device;
static int scull_u_count;   /* initialized to 0 by default, .bss section */
static uid_t scull_u_owner;
DEFINE_SPINLOCK(scull_u_lock);

static int scull_u_open(struct inode *inode, struct file *filp){
    struct scull_dev *dev = &scull_u_device;
    
    spin_lock(&scull_u_lock);
    if (scull_u_count && 
        (scull_u_owner != current_uid().val ) &&
        (scull_u_owner != current_euid().val) && 
        !capable(CAP_DAC_OVERRIDE)) {
            spin_unlock(&scull_u_lock);
            PDEBUG("current_uid %d current_euid %d\n", current_uid().val, current_euid().val);
            return -EBUSY; /* -EPERM would confuse the user */
        }

    if (scull_u_count == 0)
        scull_u_owner = current_uid().val; //rcu protect
    PDEBUG("current_uid %d current_euid %d\n", current_uid().val, current_euid().val);
    scull_u_count ++;
    spin_unlock(&scull_u_lock);

    /* then everything else is copied from the bare scull device */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_trim(dev);
    filp->private_data = dev;

    return 0; /* success */
}


static int scull_u_release(struct inode *inode, struct file *filp)
{
    spin_lock(&scull_u_lock);
    scull_u_count--;
    spin_unlock(&scull_u_lock);
    return 0;
}
/**
 *  The other operations for the single-open device come from the bare devices
 */
struct file_operations scull_user_fops = {
    .owner  =   THIS_MODULE,
    .llseek =   scull_llseek,
    .read   =   scull_read,
    .write  =   scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open   =   scull_u_open,
    .release =  scull_u_release
};


/****************************
 * 
 * Next, the device with blocking-open based on uid
 */

static struct scull_dev scull_w_device;
static int scull_w_count; /* initialized to 0 by default */
static uid_t scull_w_owner;
static DECLARE_WAIT_QUEUE_HEAD(scull_w_wait); /* wait queue */
static DEFINE_SPINLOCK(scull_w_lock);

static inline int scull_w_available(void)
{
    return scull_w_count == 0 || 
        scull_w_owner == current_uid().val ||
        scull_w_owner == current_euid().val || 
        capable(CAP_DAC_OVERRIDE);
}


static int scull_w_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev = &scull_w_device;

    spin_lock(&scull_w_lock);
    while (!scull_w_available())
    {
        spin_unlock(&scull_w_lock);
        if (filp->f_flags & O_NONBLOCK) return -EAGAIN;
        if (wait_event_interruptible(scull_w_wait, scull_w_available()))
            return -ERESTARTSYS;
        spin_lock(&scull_w_lock); /* use while-loop */
    }

    if (scull_w_count == 0)
        scull_w_owner = current_uid().val;
    scull_w_count++;

    spin_unlock(&scull_w_lock);
    
    /* then everything else is copied from the bare scull device */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_trim(dev);
    filp->private_data = dev;

    return 0; /* success */
}


static int scull_w_release(struct inode *inode, struct file *filp)
{
    int temp;

    spin_lock(&scull_w_lock);
    scull_w_count--;
    temp = scull_w_count;
    spin_unlock(&scull_w_lock);

    if (temp == 0)
        wake_up_interruptible_sync(&scull_w_wait); /* use sync not reschedule */
    return 0;
}


/**
 *  The other operations for the single-open device come from the bare devices
 */
struct file_operations scull_wusr_fops = {
    .owner  =   THIS_MODULE,
    .llseek =   scull_llseek,
    .read   =   scull_read,
    .write  =   scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open   =   scull_w_open,
    .release =  scull_w_release
};


/**************************************
 * 
 * Finally the 'cloned' private device. This is trickier because
 * it involves list management and dynamic allocation
 */

 /* The clone-specific data structure includes a key field */

struct scull_listitem {
    struct scull_dev device;
    dev_t key;
    struct list_head list;
};

/* The list of devices, and a lock to protect it */
static LIST_HEAD(scull_c_list);
static DEFINE_SPINLOCK(scull_c_lock);

/* A placeholder scull_dev which really just holds the cdev stuff */
static struct scull_dev scull_c_device;

/* Look for a device or create one if missing */
static struct scull_dev *scull_c_lookfor_device(dev_t key){
    struct scull_listitem *lptr;
    list_for_each_entry(lptr, &scull_c_list, list) {
        if (lptr->key == key)
            return &(lptr->device);
    }

    /* Not found */
    lptr = kmalloc(sizeof(struct scull_listitem), GFP_KERNEL);
    if (!lptr) 
        return NULL;
    
    /* initialize the device */
    memset(lptr, 0, sizeof(struct scull_listitem));
    lptr->key = key;
    scull_trim(&(lptr->device));
    sema_init(&(lptr->device.sem), 1);
    
    /* place it in the list*/
    list_add(&lptr->list, &scull_c_list);

    return &(lptr->device);
}


static int scull_c_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev;
    dev_t key; 

    if (!get_current_tty()) {
        PDEBUG("Process \"%s\" has to ctl tty\n", current->comm);
        return -EINVAL;
    }

    key = tty_devnum(get_current_tty());
    PDEBUG("key is %d\n", key);
    /* look for a scullc device in the list */
    spin_lock(&scull_c_lock);
    dev = scull_c_lookfor_device(key);
    spin_unlock(&scull_c_lock);

    if (!dev)
        return -ENOMEM;

    /* then everything else is copied from the bare scull device */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_trim(dev);
    filp->private_data = dev;

    return 0; /* success */
}


static int scull_c_release(struct inode *inode, struct file *filp)
{
    /**
     *  Nothing to do, because the device is persistent.
     *  A `real` cloned device should be freed on last close
     */
    return 0;
}
/*
 * The other operations for the device come from the bare device
 */
struct file_operations scull_priv_fops = {
	.owner      =   THIS_MODULE,
	.llseek     =   scull_llseek,
	.read       =   scull_read,
	.write      =   scull_write,
	.unlocked_ioctl =    scull_ioctl,
	.open       =  scull_c_open,
	.release    =  scull_c_release,
};

/**************************
 * And the init and clean up functions here
 */

static struct scull_adev_info {
    char *name;
    struct scull_dev *sculldev;
    struct file_operations *fops;
} scull_access_devs[] = {
    { "scullsingle", &scull_s_device, &scull_sngl_fops },
    { "sculluid", &scull_u_device, &scull_user_fops },
    { "scullwuid", &scull_w_device, &scull_wusr_fops },
    { "scullpriv", &scull_c_device, &scull_priv_fops }
};

#define SCULL_N_ADEVS   4

/**
 *  Setup a single device
 */
static void scull_access_setup(dev_t devno, struct scull_adev_info *devinfo){
    struct scull_dev *dev = devinfo->sculldev;
    int err;

    /* Initialize the device structure */
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    sema_init(&dev->sem, 1);

    /* Do the cdev stuff */
    cdev_init(&dev->cdev, devinfo->fops);
    //kobject_set_name(&dev->cdev.kobj, devinfo->name); /* need to be handled */
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);

    /* Fail gracefully if need be */
    if (err) {
        printk(KERN_NOTICE "Error %d adding %s\n", err, devinfo->name);
        //kobject_put(&dev->cdev.kobj);
    } else {
        printk(KERN_NOTICE "%s registered at %x\n", devinfo->name, devno);
    }
}


int scull_access_init(dev_t firstdev)
{
    int result, i;

    /* Get our number space */
    result = register_chrdev_region(firstdev, SCULL_N_ADEVS, "sculla");
    if (result < 0) {
        printk(KERN_WARNING "sculla: device number registration failed\n");
        return 0;
    }
    scull_a_firstdev = result;

    /* setup for each device */
    for (i = 0; i < SCULL_N_ADEVS; i++)
    {
        scull_access_setup(firstdev + i, scull_access_devs + i);
    }
    
    return SCULL_N_ADEVS;
}

/**
 *  This is called by cleanup_module or on failure
 *  It's required to never fail even if nothing was initialized first
 */
void scull_access_cleanup(void)
{
    struct scull_listitem *lptr, *next;
    int i;

    /* Clean up the static devs */
    for (i = 0; i < SCULL_N_ADEVS; i++)
    {
        struct scull_dev *dev = scull_access_devs[i].sculldev;
        cdev_del(&dev->cdev);
        scull_trim(scull_access_devs[i].sculldev);
    }
    
    /* And all the cloned devices */
    list_for_each_entry_safe(lptr, next, &scull_c_list, list) {
        list_del(&lptr->list);
        scull_trim(&(lptr->device));
        kfree(lptr);
    }

    /* Free up our member space */
    unregister_chrdev_region(scull_a_firstdev, SCULL_N_ADEVS);
    return;
}