/**
 * 
 *  Learn the oops messages from linux kernel 
 *  when the kernel encounters dereference null pointer 
 *  and stack buffer overflow .etc.
 * 
 * 
 * 
 */


#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/uaccess.h> /* copy_from_user and copy_to_user */

MODULE_LICENSE("GPL");


int faulty_major = 0;

ssize_t faulty_read(struct file* filp, char __user *buf,
                    size_t count, loff_t *pos)
{
    int ret;
    char stack_buf[4];
    /* Let's try a buffer overflow */
    memset(stack_buf, 0xff, 20);
    if (count > 4)
        count = 4; /* only copy 4 bytes to the user */
    ret = copy_to_user(buf, stack_buf, 4);
    if (!ret)
        return count; /* succeed */
    return ret;
}


ssize_t faulty_write(struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
    /**
     * Make a simple fault by deferencing a NULL Pointer
     */
    *(int *)0 = 0;
}


static struct file_operations faulty_ops = {
    .read = faulty_read,
    .write = faulty_write,
    .owner = THIS_MODULE
};

static int __init faulty_init(void)
{
    int result;
    /**
     * Register your major and accept a dynamic numebr
     * 
     */
    result = register_chrdev(faulty_major, "faulty", &faulty_ops);
    if (result < 0)
        return result;
    if (0 == faulty_major)
        faulty_major = result;
    
    return 0;
}



static void __exit faulty_exit(void)
{
    unregister_chrdev(faulty_major, "fualty");
}


module_init(faulty_init);
module_exit(faulty_exit);
