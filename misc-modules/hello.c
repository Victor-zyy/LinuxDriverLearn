#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>

/**
 * MODULE_LICENSE)is used to tell the
 * kernel that this module bears a free license; without such a declaration, the kernel
 * complains when the module is loaded.
 */
MODULE_LICENSE("GPL");

/**
 * Printk Function 
 * The kernel needs its own printing function because it runs by itself, 
 * without the help of the C library. 
 * The module can call printk because, after insmod has loaded it, 
 * the module is linked to the kernel 
 * and can access the kernel’s public symbols 
 * (functions and variables, as detailed in the next section).
 */

/**
 * Invoked when the module is loader into the kernel
 */
static int __init hello_init( void )
{
    /**
     * KERN_ALERT is the priority of the message
     * The priority is just a string, such as <1>, which is prepended to the printk format string. 
     * If we don't specify a high priority of this module,
     * the message would show up anywhere useful.
     */
    printk(KERN_ALERT "Hello Zynex!\r\n");
    printk(KERN_ALERT "The process is \"%s\" (pid %i)\n",
        current->comm, current->pid);
    return 0;
}


/**
 * When the module is removed
 */

static void __exit hello_exit( void )
{
    printk(KERN_ALERT "Goodbye Zynex!\r\n");
}


/**
 * Special Kernel-Macros to indicate the role of these two functions
 */

module_init(hello_init);
module_exit(hello_exit);

