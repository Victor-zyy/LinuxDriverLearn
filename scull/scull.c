/**
 * 
 * Learn scull driver -- 
 * Simple Character Utility for Loading Localities
 * 
 */


#include <linux/init.h>  // __init section .etc
#include <linux/module.h> // module_init .etc
#include <linux/moduleparam.h> //module_param .etc

#include <linux/kdev_t.h> // MAJOR/MINOR dev_t types
#include <linux/fs.h>     // register_chrdev_region
#include <linux/slab.h>  // kmalloc kfree
#include <linux/cdev.h>  //cdev function register alloc and .etc.
#include <linux/kernel.h> // container_of

#include <linux/uaccess.h> // copy_to_user or copy_from_user
#include <linux/semaphore.h> // struct semaphore
#include "scull.h"

/**
 * Our Parameters which can be set at load time
 */
int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset  = SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

MODULE_AUTHOR("Zynex Victor zyy");
MODULE_LICENSE("GPL");

/**
 * We can't set it as static number
 */
struct scull_dev *scull_devices;

/*
 * Empty out the scull device; must be called with the device
 * semaphore held.
 */
int scull_trim(struct scull_dev *dev)
{
    struct scull_qset *next, *dptr;
    int qset = dev->qset;   /* dev is not null pointer */
    int i;

    for (dptr = dev->data; dptr; dptr = next) { /* iterate all the list items */
        if (dptr->data) {
            for (i = 0; i < qset; i++)
                kfree(dptr->data[i]);
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}

/**
 * Open and close
 */

int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev; /* device information */

    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev; /* for other methods */ 

    /* Now trim to 0 the length of the device if open was write-only */
    if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
        /* Have no idea of */
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        scull_trim(dev);    /* ignore errors */
        up(&dev->sem);
    }
    return 0;       /* success */
};

int scull_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/**
 * Follow the list
 */
static struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
    struct scull_qset *qs = dev->data;

    /* Allocate first qset explicitly if need be */
    if (!qs) {
        qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
        if (qs == NULL)
            return NULL;
        memset(qs, 0, sizeof(struct scull_qset));
    }
    /* Then Follow the list */
    while (n--)
    {
        if (!qs->next) {
            qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
            if (qs->next == NULL)
                return NULL;
            memset(qs->next, 0, sizeof(struct scull_qset));
        }
        qs = qs->next;
        continue;
    }
    return qs; 
}
/**
 * Data management: read and write
 */
ssize_t scull_read(struct file *flip, char __user *buf, size_t count,
                        loff_t *f_pos)
{
    /* f_pos is the position calculated by kernel */
    struct scull_dev *dev = flip->private_data;
    struct scull_qset *dptr; /* the first listitem */
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; /* how many bytes in the listitem */
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;
    
    /* for semaphore down and up */
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    if (*f_pos >= dev->size)
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;

    /* find listitem, qset index, and offset in the quantum */
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum; q_pos = rest % quantum;

    /* follow the list up to the right position (defined eleswhere) */
    dptr = scull_follow(dev, item);

    if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
        goto out; /* don't fill holes */

    /* read only up to the end of this quantum */
    if (count > quantum - q_pos)
        count = quantum - q_pos;
    
    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += count; /* consider it*/
    retval = count;
out:
    up(&dev->sem);
    return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
                        loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM; /* value used in "goto out" statement */

    if (down_interruptible(&dev->sem)) 
        return -ERESTARTSYS;
    
    /* find listitem, qset index and offset in the quantum */
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum; q_pos = rest % quantum;

    /* follow the list up to the right position */
    dptr = scull_follow(dev, item);
    if (dptr == NULL)
        goto out;
    if (!dptr->data) {
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if (!dptr->data)
            goto out;
        memset(dptr->data, 0, qset * sizeof(char *));
    }

    if (!dptr->data[s_pos]) {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!dptr->data[s_pos])
            goto out;
    }
    /* write only up to the end of this quantum */
    if (count > quantum - q_pos)
        count = quantum - q_pos;
    
    if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count; /* consider it*/
    retval = count;

    /* update the dev->size */
    if (dev->size < *f_pos)
        dev->size = *f_pos;

out:
    up(&dev->sem);
    return retval;
}

/**
 * The ioctl() implementation
 */
long scull_ioctl(struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
    int err = 0, tmp;
    int retval = 0;

    /**
     * extract the type and number bitfields, and don't decode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC ) return -ENOTTY;
    if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

    /**
     *  the direction is a bitmask, and VERIFY_WRITE caches R/W
     *  transfers. 'Type` is user-oriented, while
     *  access_ok is kernel-oriented, so the concept of "read" and
     *  "write" is reversed
     */
    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err) return -EFAULT;

    switch (cmd)
    {
    case SCULL_IOCRESET:
        scull_quantum = SCULL_QUANTUM;
        scull_qset = SCULL_QSET;
        break;
    
    case SCULL_IOCSQUANTUM: /* Set: arg points to the value */
        if (!capable(CAP_SYS_ADMIN)) /* determine the permission of operations */
            return -EPERM;
        retval = __get_user(scull_quantum, (int __user *)arg);
        break;
    case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
        if (!capable(CAP_SYS_ADMIN)) /* determine the permission of operations */
            return -EPERM;
        scull_quantum = arg;
        break;

    case SCULL_IOCGQUANTUM: /* Get: arg is pointer to result */
        retval = __put_user(scull_quantum, (int __user *)arg);
        break;

    case SCULL_IOCQQUANTUM: /* Query: return it (it's postive) */
        return scull_quantum;

    case SCULL_IOCXQUANTUM: /* Exchange: use arg as pointer */
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = scull_quantum;
        retval = __get_user(scull_quantum, (int __user *)arg);
        if (retval == 0)
            retval = __put_user(tmp, (int __user *)arg);
        break;
    
    case SCULL_IOCHQUANTUM: /* Shift: Like Tell + Query */
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = scull_quantum;
        scull_quantum = arg;
        return tmp;

    case SCULL_IOCSQSET:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        retval = __get_user(scull_qset, (int __user *)arg);
        break;
    
    case SCULL_IOCTQSET:
        if (!capable(CAP_SYS_ADMIN)) 
           return -EPERM;
        scull_qset = arg;
        break;
        
    case SCULL_IOCGQSET:
        retval = __put_user(scull_qset, (int __user *)arg);
        break;

    case SCULL_IOCQQSET:
        return scull_qset;
    
    case SCULL_IOCXQSET:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = scull_qset;
        retval = __get_user(scull_qset, (int __user *)arg);
        if (retval == 0)
            retval = put_user(tmp, (int __user *)arg);
        break;

    case SCULL_IOCHQSET:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = scull_qset;
        scull_qset = arg;
        return tmp;

        /*
         * The following two change the buffer size for scullpipe.
         * The scullpipe device uses this same ioctl method, just to
         * write less code. Actually, it's the same driver, isn't it?
         */

	  case SCULL_P_IOCTSIZE:
		//scull_p_buffer = arg;
		break;

	  case SCULL_P_IOCQSIZE:
        break;
		//return scull_p_buffer;


    default: /* redundant, as cmd was checked against MAXNR */
        return -ENOTTY;
    }

    return retval;
}

/*
 * The "extended" operations -- only seek
 */

loff_t  scull_llseek(struct file *filp, loff_t off, int whence)
{
    struct scull_dev *dev = filp->private_data;
    loff_t newpos;

    /* To see the man page of lseek */
    switch(whence) {
        case 0: /* SEEK_SET*/
            newpos = off;
            break;
        
        case 1: /* SEEK_CUR */
            newpos = filp->f_pos + off;
            break;
        
        case 2: /* SEEK_END */
            newpos = dev->size + off;
            break;
        
        default: /* can't happen */
            return -EINVAL;
    }

    if (newpos < 0) return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}


struct file_operations scull_ops = {
    .owner =    THIS_MODULE,
    .llseek =   scull_llseek,
    .read =     scull_read,
    .write =    scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open =     scull_open,
    .release =  scull_release,
};


/*
 * Finally, the module stuff
 */

static void scull_setup_cdev(struct scull_dev *dev, int index)
{
    int err, devno = MKDEV(scull_major, scull_minor + index);

    cdev_init(&dev->cdev, &scull_ops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_ops;
    err = cdev_add(&dev->cdev, devno, 1);
    /*Failed gracefully if need be */
    if (err)
        printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */

static void scull_cleanup_module(void) // indeed the other function use this so don't define it into .exit.text section
{  
    int i;
    dev_t devno = MKDEV(scull_major, scull_minor);

    /* Get rid of our char dev entries */
    if (scull_devices) {
        for (i = 0; i < scull_nr_devs; i++) {
            scull_trim(scull_devices + i);
            cdev_del(&scull_devices[i].cdev);
        }
        kfree(scull_devices);
    }

#ifdef SCULL_DEBUG /* use proc only if debugging */
    scull_remove_proc();
#endif

	/* cleanup_module is never called if registering failed */
    unregister_chrdev_region(devno, scull_nr_devs);

    /* and call the cleanup functions for friend devices */
    //scull_p_cleanup(); // for scull_pipe
    //scull_access_cleanup(); // for scull_access

}

static int __init scull_init_module(void)
{
    int result, i;
    dev_t dev = 0;

/*
 * Get a range of minor numbers to work with, asking for a dynamic
 * major unless directed otherwise at load time.
 */
    if (scull_major) {
        // If we assign the major number of scull
        dev = MKDEV(scull_major, scull_minor);
        // obtain device number one or more
        result = register_chrdev_region(dev, scull_nr_devs,
                "scull");
    } else {
        // we don't know exactly what the device numbers you want
        result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs,
                "scull");
        scull_major = MAJOR(dev);
    }

    if (result < 0) {
        printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
        return result;
    }
/* 
* allocate the devices -- we can't have them static, as the number
* can be specified at load time
*/
    scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
    if (!scull_devices) {
        result = -ENOMEM;
        goto fail; // Makes this more graceful
    }

    /* Initialize each device */
    for (i = 0; i < scull_nr_devs; i++) {
        scull_devices[i].quantum = scull_quantum;
        scull_devices[i].qset = scull_qset;
        sema_init(&scull_devices[i].sem, 1);   // semaphore value is one
        scull_setup_cdev(&scull_devices[i], i);
    }
    /* At this point call the init function for any friend device */
    dev = MKDEV(scull_major, scull_minor + scull_nr_devs); 
    //dev += scull_p_init(dev);       // scull_pipe
    //dev += scull_access_init(dev);  // scull_access

#ifdef SCULL_DEBUG
    scull_create_proc(); /* Only when debugging */
#endif

    return 0; /* Succeed */

fail:
    scull_cleanup_module();
    return result;
}


module_init(scull_init_module);
module_exit(scull_cleanup_module);