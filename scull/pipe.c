/**
 * 
 * pipe.c -- fifo driver for scull
 * 
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <asm/uaccess.h> /* get_user put_user */
#include <linux/sched/signal.h>

#include "scull.h" /* local file */


struct scull_pipe{
    wait_queue_head_t inq, outq;        /* read and write queues */
    char *buffer, *end;                 /* begin of buf, end of buf*/
    int buffersize;                     /* used in pointer arithmetic */
    char *rp, *wp;                      /* where to read, where to write */
    int nreaders, nwriters;             /* number of openings for r/w */
    struct fasync_struct *async_queue;  /* asynchronous readers */
    struct semaphore sem;               /* mutual exclusion semaphore */
    struct cdev cdev;                   /* Char device structure */
};

/* parameters */
static int scull_p_nr_devs = SCULL_P_NR_DEVS;   /* number of pipe devices */
int scull_p_buffer  =  SCULL_P_BUFFER;          /* buffer size */
dev_t scull_p_devno;                            /* Our first device number */

module_param(scull_p_nr_devs, int, 0);          /* Fixme check perms */
module_param(scull_p_buffer, int, 0);

static struct scull_pipe *scull_p_devices;

static int scull_p_fasync(int fd, struct file *filp, int mode)
{
    struct scull_pipe *dev = filp->private_data;
    
    return fasync_helper(fd, filp, mode, &dev->async_queue);
}
/**
 * open and close
 */

static int scull_p_open(struct inode *inode, struct file *filp)
{
    struct scull_pipe *dev;
    dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
    filp->private_data = dev;

    if (down_interruptible(&dev->sem)) {
        return -ERESTARTSYS;
    }
    if (!dev->buffer) {
        /* allocate the buffer */
        dev->buffer = kmalloc(scull_p_buffer, GFP_KERNEL);
        if (!dev->buffer) {
            up(&dev->sem);
            return -ENOMEM;
        }
    }

    dev->buffersize = scull_p_buffer;
    dev->end = dev->buffer + dev->buffersize;
    dev->rp = dev->wp = dev->buffer; /* wp = rp from begining */

    /* use f_mode not f_flags it's cleaner --- fs/open.c need to be handler*/
    if (filp->f_mode & FMODE_READ) {
        dev->nreaders++;
    }
    if (filp->f_mode & FMODE_WRITER) {
        dev->nwriters++;
    }
    up(&dev->sem);

    return nonseekable_open(inode, filp); // to indicate that this device doesn't support llseek

}

static int scull_p_release(struct inode *inode, struct file *filp)
{
    struct scull_pipe *dev = filp->private_data;

    /* remove this filp from asynchronously notified filp's */
    scull_p_fasync(-1, filp, 0);
    down(&dev->sem); /* why use down not down_interruptible */
    if (filp->f_mode & FMODE_READ)
        dev->nreaders--;
    if (filp->f_mode & FMODE_WRITE)
        dev->nwriters--;
    if (dev->nreaders + dev->nwriters == 0) {
        kfree(dev->buffer);
        dev->buffer = NULL; /* The other field are not checked */
    }
    up(&dev->sem);
    return 0;
}


/**
 * 
 * Data management : read and write
 * 
 */

static ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count, loff_t *pos)
{
    struct scull_pipe *dev = filp->private_data;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    
    while (dev->rp == dev->wp) { /* nothing to read */
        up(&dev->sem);
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN; /*not support blocking IO*/
        PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
        if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
            return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        /* otherwise loop: but accquire the lock first */
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }
    /* data is ok, read here */
    if (dev->wp > dev->rp)
        count = min(count, (size_t)(dev->wp - dev->rp));
    else /* write pointer has wrappered */
        count = min(count, (size_t)(dev->end - dev->rp));
    if (copy_to_user(buf, dev->rp, count)){
        up(&dev->sem);
        return -EFAULT;
    }
    dev->rp += count;
    if (dev->rp == dev->end)
        dev->rp = dev->buffer; /* wrapped */
    up(&dev->sem);

    /* Finally, awake any writers and return */
    wake_up_interruptible(&dev->outq);
    PDEBUG("\"%s\" did read %li bytes\n", current->comm, (long)count);
    return count;
}


/* How much space is free */
static int spacefree(struct scull_pipe *dev)
{
    if (dev->rp == dev->wp)
        return dev->buffersize - 1;
    return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize ) - 1;
}
/**
 * Wait for space for writing; caller must hold device semaphore;
 * On error the semaphore will be released before returning.
 */
static int scull_getwritespace(struct scull_pipe *dev, struct file *filp)
{
    while (spacefree(dev) == 0) /* full */
    {
        /* manually going to sleep */
        DEFINE_WAIT(wait);
        
        up(&dev->sem);
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        PDEBUG("\"%s\" writing: going to sleep\n", current->comm);
        prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
        if (spacefree(dev) == 0) /* need to do next condition */
            schedule();
        finish_wait(&dev->outq, &wait);
        if (signal_pending(current))
            return -ERESTARTSYS; /*signal: tell the fs layer to handler it */
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        
    }
    return 0; 
}


static ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
    struct scull_pipe *dev = filp->private_data;
    int result;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    /* make sure there's space to write */
    result = scull_getwritespace(dev, filp);
    if (result)
        return result; /* sem has released in that function */

    /* ok space is there, accept something */
    count = min(count, (size_t)spacefree(dev));
    if (dev->wp >= dev->rp)
        count = min(count, (size_t)(dev->end - dev->wp)); /* end-of buffer */
    else /* the write pointer has wrapped, fill up to rp - 1 */
        count = min(count, (size_t)(dev->rp - dev->wp - 1));
    PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
    if (copy_from_user(dev->wp, buf, count)) {
        up(&dev->sem);
        return -EFAULT;
    }
    dev->wp += count;
    if (dev->wp == dev->end)
        dev->wp = dev->buffer; /* wrapped */
    up(&dev->sem);

    /* finally awake any readers */
    wake_up_interruptible(&dev->inq);
    /* signal async readers */
    if (dev->async_queue)
        kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
    /**
     * Async and Wait order need to be considered
     * by default O_NONBLOCKIING is not set, so blocking io
     */
    PDEBUG("\"%s\" did write %li bytes\n", current->comm, (long)count);
    return count;
}


static unsigned int scull_p_poll(struct file *filp, poll_table *wait)
{
    struct scull_pipe *dev = filp->private_data;
    unsigned int mask = 0;

    /**
     * The buffer is circular; it's considered full if "wp" 
     * is right behind "rp", and empty if the two are equal
     */
    down(&dev->sem);
    poll_wait(filp, &dev->inq, wait); /* this is not sleep at all */
    poll_wait(filp, &dev->outq, wait); 
    if (dev->rp != dev->wp)
        mask |= POLLIN | POLLRDNORM; /* readable */
    if (spacefree(dev))
        mask |= POLLOUT | POLLWRNORM; /* writable */
    up(&dev->sem);
    return mask;
}

/**
 * The file operations for the pipe device
 */
static struct file_operations scull_pipe_fops = {
    .owner      =   THIS_MODULE,
    .llseek     =   no_llseek,
    .read       =   scull_p_read,
    .write      =   scull_p_write,
    .poll       =   scull_p_poll,
    .unlocked_ioctl = scull_ioctl,
    .open       =   scull_p_open,
    .release    =   scull_p_release,
    .fasync     =   scull_p_fasync,
};

/**
 * Set up a cdev entry
 */

static void scull_p_setup_cdev(struct scull_pipe *dev, int index)
{
    int err, devno = scull_p_devno + index;

    cdev_init(&dev->cdev, &scull_pipe_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);
    /* Fail gracefully if need be */
    if (err)
        printk(KERN_NOTICE "Error %d adding scullpipe%d", err, index);
}


/**
 * Initialize the pipe devs; return how many we did
 */

int scull_p_init(dev_t firstdev)
{
    int i, result;

    result = register_chrdev_region(firstdev, scull_p_nr_devs, "scullp");
    if (result < 0) {
        printk(KERN_NOTICE "Unable to get scullp region, error %d\n", result);
        return 0;
    }
    scull_p_devno = firstdev;
    scull_p_devices = kmalloc(scull_p_nr_devs * sizeof(struct scull_pipe), GFP_KERNEL);
    if (scull_p_devices == NULL) {
        unregister_chrdev_region(firstdev, scull_p_nr_devs);
        return 0;
    }
    memset(scull_p_devices, 0, scull_p_nr_devs * sizeof(struct scull_pipe));
    for (i = 0; i < scull_p_nr_devs; i++)
    {
        init_waitqueue_head(&scull_p_devices[i].inq);
        init_waitqueue_head(&scull_p_devices[i].outq);
        sema_init(&scull_p_devices[i].sem, 1);
        scull_p_setup_cdev(scull_p_devices + i, i);
    }
    /* for proc filesystem */ 
    return scull_p_nr_devs;
}


/**
 * THis is called by cleanup_module or on failure
 * It is required to never fail, even if nothing was
 * initialized first
 */
void scull_p_cleanup(void)
{
    int i ;

    /* remove proc file */

    if (!scull_p_devices)
        return;
    
    for (i = 0; i < scull_p_nr_devs; i++) {
        cdev_del(&scull_p_devices[i].cdev);
        kfree(scull_p_devices[i].buffer);
    }
    kfree(scull_p_devices);
    unregister_chrdev_region(scull_p_devno, scull_p_nr_devs);
    scull_p_devices = NULL;
}