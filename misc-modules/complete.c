/**
 * 
 *  complete.c -- the writers awake the readers
 * 
 * 
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h> // printk
#include <linux/sched.h> // current
#include <linux/fs.h>

#include <linux/types.h> // ssize_t
#include <linux/completion.h>


MODULE_LICENSE("GPL");

static int complete_major = 0;

DECLARE_COMPLETION(comp);

ssize_t complete_read (struct file *flip, char __user *buf, size_t count, loff_t *pos) {
    printk(KERN_DEBUG "process %i (%s) going to sleep\n",
        current->pid, current->comm);
    wait_for_completion(&comp);
    printk(KERN_DEBUG "awoken %i (%s)\n", current->pid, current->comm);
    return 0; /* EOF */
}


ssize_t complete_write (struct file *flip, const char __user *buf, size_t count, loff_t *pos) {
    printk(KERN_DEBUG "process %i (%s) awakening the readers.....\n",
        current->pid, current->comm);
    complete(&comp);
    return count; /* succeed, to avoid retrial */
}
static struct file_operations  complete_fops = {
    .owner = THIS_MODULE,
    .read  = complete_read,
    .write = complete_write,
};


static int __init complete_init(void)
{
    int result;

    /**
     * register your major, and accept a dynamic number
     */
    result = register_chrdev(complete_major, "complete", &complete_fops);
    if (result < 0)
        return result;
    if (complete_major == 0)
        complete_major = result;
    return 0;
}


static void __exit complete_exit(void)
{
    unregister_chrdev(complete_major, "complete");
}


module_init(complete_init);
module_exit(complete_exit);