#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("GPL");


/**
 *  Test with A module parameters
 */


static char *whom = "world";
static int howmany = 1;
module_param(howmany, int, S_IRUGO); // U: user G: group O: other R: read
module_param(whom, charp, S_IRUGO);  


static int __init hello_init(void)
{
    int i = 0;
    for (i = 0; i < howmany; i++ )
        printk(KERN_ALERT "(%d) Hello, %s\n", i, whom);
    return 0;
}


static void __exit hello_exit(void)
{
    printk(KERN_ALERT "Goodbye, curel world\n");
}

module_init(hello_init);
module_exit(hello_exit);