
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
#include <linux/fs.h>     // register_chrdev_region -- everything
#include <linux/uio.h>
#include <linux/slab.h>  // kmalloc kfree
#include <linux/cdev.h>  //cdev function register alloc and .etc.
#include <linux/kernel.h> // container_of
#include <linux/vmalloc.h> // vmalloc

#include <linux/uaccess.h> // copy_to_user or copy_from_user
#include <linux/semaphore.h> // struct semaphore
#include <linux/proc_fs.h>  // read_procmem
#include <linux/seq_file.h> // seq_file stack

#include "scull.h"


//#define SCULLV_DEBUG /* Just for Test The true definition is defined in Makefile */

/**
 * Our Parameters which can be set at load time
 */
int scullv_major = SCULLV_MAJOR;
int scullv_devs  = SCULLV_DEVS;
int scullv_qset  = SCULLV_QSET;
int scullv_order = SCULLV_ORDER;

module_param(scullv_major, int, S_IRUGO);
module_param(scullv_devs, int, S_IRUGO);
module_param(scullv_qset, int, S_IRUGO);
module_param(scullv_order, int, S_IRUGO);

MODULE_AUTHOR("Zynex Victor zyy");
MODULE_LICENSE("GPL");

/**
 * We can't set it as static number
 */
struct scullv_dev *scullv_devices = NULL;


/*
 * Empty out the scull device; must be called with the device
 * semaphore held.
 */
int scullv_trim(struct scullv_dev *dev)
{
    struct scullv_dev *next, *dptr;
    int qset = dev->qset;   /* dev is not null pointer */
    int i;

    if (dev->vmas) /* don't trim there are avtive mappings */
        return -EBUSY;
    for (dptr = dev; dptr; dptr = next) { /* iterate all the list items */
        if (dptr->data) {
            for (i = 0; i < qset; i++){
                if (dptr->data[i]) {
                    //kfree(dptr->data[i]);
                    vfree(dptr->data[i]);
                }
            }
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        if (dptr != dev ) kfree(dptr); /* all of them but the first */
    }
    dev->size = 0;
    dev->order = scullv_order;
    dev->qset = scullv_qset;
    dev->next = NULL;
    return 0;
}

#ifdef SCULLV_DEBUG /* Use proc filesystem if debugging */

/**
 * For now the seq_file implementation will exist in parallel. The older
 * read_procmem function should maybe go away, though.
 */

/**
 * Here are our sequence iteration methods. Our "position" is simply the
 * device number.
 */

static void *scullv_seq_start(struct seq_file *s, loff_t *pos)
{
    if (*pos >= scullv_devs)
        return NULL;
    return scullv_devices + *pos;
}

static void *scullv_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    (*pos)++;
    if (*pos >= scullv_devs)
        return NULL;
    return scullv_devices + *pos;
}

static void scullv_seq_stop(struct seq_file *s, void *v)
{
    /* Actually we do have nothing here*/
}

static int scullv_seq_show(struct seq_file *s, void *v)
{
    struct scullv_dev *dev = (struct scullv_dev *)v; // address convert
    struct scullv_dev *d = dev;
    int quantum = PAGE_SIZE << dev->order;
    int qset = dev->qset;
    int i;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    /*scan the list*/
    seq_printf(s, "\nDevice %i: qset %i, quantum %i, sz %li\n", (int)(dev - scullv_devices),
                qset, quantum, (long)dev->size);
    for (; d; d = d->next) { /* scan the list */ 
        seq_printf(s, "  item at %p, qset at %p\n", d, d->data);
        if (d->data && !d->next) /* Dump only the last item*/
            for (i = 0; i < dev->qset; i++) {
                if (d->data[i]) 
                    seq_printf(s, "    % 4i: %8p\n",
                                i, d->data[i]);
            }
    }
    up(&dev->sem);
    return 0;
}

/**
 * Tie the sequence operations up
 */

/*
struct seq_operations {
	void * (*start) (struct seq_file *m, loff_t *pos);
	void (*stop) (struct seq_file *m, void *v);
	void * (*next) (struct seq_file *m, void *v, loff_t *pos);
	int (*show) (struct seq_file *m, void *v);
};
*/
static struct seq_operations scullv_seq_ops = {
    .start =scullv_seq_start,
    .next = scullv_seq_next,
    .stop = scullv_seq_stop,
    .show = scullv_seq_show
};

/**
 * Now to implement the /proc file we need only make on open method
 * which sets up the sequence file operations
 */
static int scullv_proc_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &scullv_seq_ops);
}

/**
 * Create a set of file operations for our proc file
 */
static struct file_operations scullv_proc_ops = {
    .owner      = THIS_MODULE,
    .open       = scullv_proc_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = seq_release
};

/**
 * Actually create and remove the /proc file(s).
 */
static void scullv_create_proc(void)
{
    struct proc_dir_entry *entry;
    /* this function has been deprecated */
    #if 0
    create_proc_read_entry("scullmem", 0 /* default mode */,
            NULL /* parent dir */, scull_read_procmem,
            NULL /* client data */);
    #endif
    /* create_proc_entry is deprecated since kernel version3.1 now is changed to proc_create*/
    entry = proc_create("scullvseq", 0, NULL, &scullv_proc_ops); 
}

static void scullv_remove_proc(void)
{
    /* no problem if it was not registered */
    //remove_proc_entry("scullmem", NULL /* parent dir */);
    remove_proc_entry("scullvseq", NULL);
}
#endif /* SCULL_DEBUG */

/**
 * Open and close
 */

int scullv_open(struct inode *inode, struct file *filp)
{
    struct scullv_dev *dev; /* device information */

    dev = container_of(inode->i_cdev, struct scullv_dev, cdev);

    /* Now trim to 0 the length of the device if open was write-only */
    if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
        /* Have no idea of */
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        scullv_trim(dev);    /* ignore errors */
        up(&dev->sem);
    }
    /* and use filp->private_data to point to the device data */
    filp->private_data = dev; /* for other methods */ 
    return 0;       /* success */
};

int scullv_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/**
 * Follow the list
 */
struct scullv_dev *scullv_follow(struct scullv_dev *dev, int n)
{

    /* Then Follow the list */
    while (n--)
    {
        if (!dev->next) {
            dev->next = kmalloc(sizeof(struct scullv_dev), GFP_KERNEL);
            memset(dev->next, 0, sizeof(struct scullv_dev));
        }
        dev = dev->next;
        continue;
    }
    return dev; 
}
/**
 * Data management: read and write
 */
ssize_t scullv_read(struct file *flip, char __user *buf, size_t count,
                        loff_t *f_pos)
{
    /* f_pos is the position calculated by kernel */
    struct scullv_dev *dev = flip->private_data;
    struct scullv_dev *dptr; /* the first listitem */
    int quantum = PAGE_SIZE << dev->order, qset = dev->qset;
    int itemsize = quantum * qset; /* how many bytes in the listitem */
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;
    
    /* for semaphore down and up */
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    if (*f_pos >= dev->size)
        goto nothing;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;

    /* find listitem, qset index, and offset in the quantum */
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum; q_pos = rest % quantum;

    /* follow the list up to the right position (defined eleswhere) */
    dptr = scullv_follow(dev, item);

    if (!dptr->data)
        goto nothing;
    if (!dptr->data[s_pos])
        goto nothing;

    /* read only up to the end of this quantum */
    if (count > quantum - q_pos)
        count = quantum - q_pos;
    
    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
        retval = -EFAULT;
        goto nothing;
    }

    *f_pos += count; /* consider it*/
    retval = count;

nothing:
    up(&dev->sem);
    return retval;
}

ssize_t scullv_write(struct file *filp, const char __user *buf, size_t count,
                        loff_t *f_pos)
{
    struct scullv_dev *dev = filp->private_data;
    struct scullv_dev *dptr;
    int quantum = PAGE_SIZE << dev->order, qset = dev->qset;
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
    dptr = scullv_follow(dev, item);
    if (dptr == NULL)
        goto out;
    if (!dptr->data) {
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if (!dptr->data)
            goto out;
        memset(dptr->data, 0, qset * sizeof(char *));
    }

    /* here is the allocation of a single quantum */
    if (!dptr->data[s_pos]) {
        dptr->data[s_pos] = (void *)vmalloc(GFP_KERNEL);
        if (!dptr->data[s_pos])
            goto out;
        memset(dptr->data[s_pos], 0, PAGE_SIZE << dptr->order);
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
long scullv_ioctl(struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
    int err = 0, tmp;
    int retval = 0;

    /**
     * extract the type and number bitfields, and don't decode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if (_IOC_TYPE(cmd) != SCULLV_IOC_MAGIC ) return -ENOTTY;
    if (_IOC_NR(cmd) > SCULLV_IOC_MAXNR) return -ENOTTY;

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
    case SCULLV_IOCRESET:
        scullv_order = SCULLV_ORDER;
        scullv_qset  = SCULLV_QSET;
        break;
    
    case SCULLV_IOCSORDER: /* Set: arg points to the value */
        if (!capable(CAP_SYS_ADMIN)) /* determine the permission of operations */
            return -EPERM;
        retval = __get_user(scullv_order, (int __user *)arg);
        break;
    case SCULLV_IOCTORDER: /* Tell: arg is the value */
        if (!capable(CAP_SYS_ADMIN)) /* determine the permission of operations */
            return -EPERM;
        scullv_order = arg;
        break;

    case SCULLV_IOCGORDER: /* Get: arg is pointer to result */
        retval = __put_user(scullv_order, (int __user *)arg);
        break;

    case SCULLV_IOCQORDER: /* Query: return it (it's postive) */
        return scullv_order;

    case SCULLV_IOCXORDER: /* Exchange: use arg as pointer */
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = scullv_order;
        retval = __get_user(scullv_order, (int __user *)arg);
        if (retval == 0)
            retval = __put_user(tmp, (int __user *)arg);
        break;
    
    case SCULLV_IOCHORDER: /* Shift: Like Tell + Query */
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = scullv_order;
        scullv_order = arg;
        return tmp;

    case SCULLV_IOCSQSET:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        retval = __get_user(scullv_qset, (int __user *)arg);
        break;
    
    case SCULLV_IOCTQSET:
        if (!capable(CAP_SYS_ADMIN)) 
           return -EPERM;
        scullv_qset = arg;
        break;
        
    case SCULLV_IOCGQSET:
        retval = __put_user(scullv_qset, (int __user *)arg);
        break;

    case SCULLV_IOCQQSET:
        return scullv_qset;
    
    case SCULLV_IOCXQSET:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = scullv_qset;
        retval = __get_user(scullv_qset, (int __user *)arg);
        if (retval == 0)
            retval = put_user(tmp, (int __user *)arg);
        break;

    case SCULLV_IOCHQSET:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = scullv_qset;
        scullv_qset = arg;
        return tmp;

        /*
         * The following two change the buffer size for scullvipe.
         * The scullvipe device uses this same ioctl method, just to
         * write less code. Actually, it's the same driver, isn't it?
         */

    default: /* redundant, as cmd was checked against MAXNR */
        return -ENOTTY;
    }

    return retval;
}

/*
 * The "extended" operations -- only seek
 */

loff_t  scullv_llseek(struct file *filp, loff_t off, int whence)
{
    struct scullv_dev *dev = filp->private_data;
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


/**
 *  Asynchronous I/O --- chapter15 to learn AIO Framework
 *  For memory map --- Chapter15 to learn and review
 * 
 */
static ssize_t scullv_aio_read(struct kiocb *iocb, struct iov_iter *iov)
{

}
static ssize_t scullv_aio_write(struct kiocb *iocb, struct iov_iter *iov)
{

}

//ssize_t (*read_iter) (struct kiocb *, struct iov_iter *);
//ssize_t (*write_iter) (struct kiocb *, struct iov_iter *);
struct file_operations scullv_fops = {
    .owner =    THIS_MODULE,
    .llseek =   scullv_llseek,
    .read =     scullv_read,
    .write =    scullv_write,
    .unlocked_ioctl = scullv_ioctl,
    .open =     scullv_open,
    .release =  scullv_release,
    .read_iter = scullv_aio_read,
    .write_iter = scullv_aio_write,
};


/*
 * Finally, the module stuff
 */

static void scullv_setup_cdev(struct scullv_dev *dev, int index)
{
    int err, devno = MKDEV(scullv_major, 0 + index);

    cdev_init(&dev->cdev, &scullv_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scullv_fops;
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

/* indeed the other function use this so don't define it into .exit.text section */ 

static void scullv_cleanup_module(void) 
{  
    int i;
    dev_t devno = MKDEV(scullv_major, 0);

    /* Get rid of our char dev entries */
    if (scullv_devices) {
        for (i = 0; i < scullv_devs; i++) {
            cdev_del(&scullv_devices[i].cdev);
            scullv_trim(scullv_devices + i);
        }
        kfree(scullv_devices);
    }

#ifdef SCULLV_DEBUG /* use proc only if debugging */
    scullv_remove_proc();
#endif
	/* cleanup_module is never called if registering failed */
    unregister_chrdev_region(devno, scullv_devs);
}

static int __init scullv_init_module(void)
{
    int result, i;
    dev_t dev = 0;

/*
 * Get a range of minor numbers to work with, asking for a dynamic
 * major unless directed otherwise at load time.
 */
    if (scullv_major) {
        // If we assign the major number of scull
        dev = MKDEV(scullv_major, 0);
        // obtain device number one or more
        result = register_chrdev_region(dev, scullv_devs,
                "scullv");
    } else {
        // we don't know exactly what the device numbers you want
        result = alloc_chrdev_region(&dev, 0, scullv_devs,
                "scullv");
        scullv_major = MAJOR(dev);
    }

    if (result < 0) {
        printk(KERN_WARNING "scull: can't get major %d\n", scullv_major);
        return result;
    }
/* 
* allocate the devices -- we can't have them static, as the number
* can be specified at load time
*/
    scullv_devices = kmalloc(scullv_devs * sizeof(struct scullv_dev), GFP_KERNEL);
    if (!scullv_devices) {
        result = -ENOMEM;
        goto fail_malloc; // Makes this more graceful
    }
    /* Need to be considered when u allocate a new memory space
     * If I don't memset this memory, when calling rmmod and open file,
     * it will call scull_trim, and the address may be not null
     * Then kfree will accidentally free the unexpected memory which 
     * cause oops in linux kernel(err :5)
     */
    memset(scullv_devices, 0, sizeof(struct scullv_dev) * scullv_devs);

    /* Initialize each device */
    for (i = 0; i < scullv_devs; i++) {
        scullv_devices[i].order = scullv_order;
        scullv_devices[i].qset = scullv_qset;
        sema_init(&scullv_devices[i].sem, 1);   // semaphore value is one
        scullv_setup_cdev(&scullv_devices[i], i);
    }


#ifdef SCULLV_DEBUG
    scullv_create_proc(); /* Only when debugging */
#endif

    return 0; /* Succeed */

fail_malloc:
    scullv_cleanup_module();
    return result;
}


module_init(scullv_init_module);
module_exit(scullv_cleanup_module);