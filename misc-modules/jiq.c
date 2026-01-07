/**
 *  jiq.c -- just in workqueue
 * 
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/errno.h> /* error codes */
#include <linux/interrupt.h> /* tasklets */
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");

/**
 * The delay for the delayed workqueue timer file
 */
static long delay = 10;
module_param(delay, long, 0);

/**
 * This module is a silly one: it only embeds short code fragments
 * that show how enqueued tasks 'feel' the environment
 */

#define LIMIT   (PAGE_SIZE-128) /* don't print any more after this size */

/**
 * Print information about the current environment. This is called from
 * within the task queues. If the limit is reached, awake the readingh 
 * process.
 */
static DECLARE_WAIT_QUEUE_HEAD(jiq_wait);


/**
 * Keep track of info we need between task queue runs.
 */
static struct clientdata {
    int len;
    char *buf;
    unsigned long jiffies;
    long delay;
    struct timer_list jiq_timer;
    struct seq_file *seq;
    struct delayed_work jiq_work;
}jiq_data;
// #define SCHEDULER_QUEUE (task_que)

/**
 * Do the printing; return non-zero if the task should be rescheduled
 */
static int jiq_print(void *ptr)
{
    struct clientdata *data = ptr;
    struct seq_file *seq = data->seq;
    
    int len = data->len;
    unsigned long j = jiffies; 

    if (len > LIMIT) {
        //wake_up_interruptible(&jiq_wait); /* who raise alarm to this ?*/
        return 0;
    }

    if (len == 0) {
        seq_printf(seq, "    time  delta preempt   pid cpu command\n\r");
        len += seq->count;
    } else {
        len = 0; 
    }
    //pr_info("jiq_print seq->count len %d\n\r", seq->count); 
    /* intr_count is only exported since 1.3.5 but 1.99.4 is needed anyways */
    seq_printf(seq, "%9li  %4li    %3i %5i %3i %s\n",
            j, j - data->jiffies, 
            preempt_count(), current->pid, smp_processor_id(),
            current->comm);
    //pr_info("jiq_print seq->count len %d\n\r", seq->count); 
    len += seq->count;
    data->len += len;
    data->jiffies = j;
    return 1;
}
static void jiq_print_wq(struct work_struct *work)
{
    struct clientdata *data = container_of(to_delayed_work(work), struct clientdata, jiq_work);
    
    if (!jiq_print(data))
        return;
    
    if (data->delay)
        schedule_delayed_work(&data->jiq_work, data->delay);
    else 
        schedule_work(&data->jiq_work.work);
}

static int jiq_read_wq(struct seq_file *seq, void *m)
{
    DEFINE_WAIT(wait);

    jiq_data.len = 0;   /* nothing printed yet */
    jiq_data.seq = seq; /* print in this place */
    jiq_data.jiffies = jiffies; /* initial time */
    jiq_data.delay = 0;

    /**
     * These steps push the work to work_queue
     */
    prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
    schedule_work(&jiq_data.jiq_work.work);
    schedule();
    finish_wait(&jiq_wait, &wait);

    return 0;
}

static int jiq_read_wq_delayed(struct seq_file *seq, void *m)
{

    DEFINE_WAIT(wait);

    jiq_data.len = 0;   /* nothing printed yet */
    jiq_data.seq = seq; /* print in this place */
    jiq_data.jiffies = jiffies; /* initial time */
    jiq_data.delay = delay;

    /**
     * These steps push the work to work_queue
     */
    prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
    schedule_delayed_work(&jiq_data.jiq_work, delay);
    schedule();
    finish_wait(&jiq_wait, &wait);

    return 0;
}

/**
 * 
 *  jiq_timer --- tests out the timers
 * 
 */
static int var = 0;
static void jiq_timeout(struct timer_list *t)
{
    jiq_print((void *)(from_timer(&jiq_data, t, jiq_timer)));
    var = 1;
    pr_info("happend there\n");
    wake_up_interruptible(&jiq_wait);
}

static int jiq_read_run_timer(struct seq_file *seq, void *m)
{
    jiq_data.len = 0; /* prepare the argument for jiq */
    jiq_data.jiffies = jiffies;
    jiq_data.seq = seq;

    timer_setup(&jiq_data.jiq_timer, jiq_timeout, 0);
    jiq_data.jiq_timer.expires = jiffies + HZ; /* for about one second */

    jiq_print(&jiq_data); /* print and goto sleep */
    add_timer(&jiq_data.jiq_timer);
     
    /**
     * interruptible_sleep_on()
     * was inherently racy because it did not check a condition before sleeping. 
     * If an event occurred (and a wakeup was signaled) 
     * between the time a driver decided to sleep and the time 
     * it actually called the function, the wakeup would be "lost," 
     * and the process might sleep forever.
     */
    wait_event_interruptible(jiq_wait, var != 0);
    var = 0;
    pr_info("jiq_read_run_timer goes into there\n");
    del_timer_sync(&jiq_data.jiq_timer); 
    return 0;
}

/**
 * 
 * 
 */
static int var_2 = 0;
static void jiq_print_tasklet(unsigned long);
static DECLARE_TASKLET(jiq_tasklet, jiq_print_tasklet, (unsigned long)&jiq_data);

static void jiq_print_tasklet(unsigned long ptr)
{
    if (jiq_print((void *)ptr))
        tasklet_schedule(&jiq_tasklet);
    else {
        //var_2 = 1;
    }
}


static int jiq_read_tasklet(struct seq_file *seq, void *m)
{

    jiq_data.len = 0;
    jiq_data.jiffies = jiffies;
    jiq_data.seq = seq;

    tasklet_schedule(&jiq_tasklet); // tasklet is so fast that until the var_2 is set we haven't let our next function to run
    /**
     * The truth is seq_file only format string to a buffer,
     * and after show and next iterator is done, then output.
     */
    wait_event_interruptible(jiq_wait, var_2 != 0); /* sleep till complete exceed size */
    pr_info("var_2 %d jiq_read_tasklet exit\n", var_2); /* need to be considered and seq_file function */
    var_2 = 0;
    return 0;
}

static int __init jiq_init(void)
{

    struct proc_dir_entry *ret; 
    /* init work */
    INIT_WORK(&jiq_data.jiq_work.work, jiq_print_wq);
    INIT_DELAYED_WORK(&jiq_data.jiq_work, jiq_print_wq);
    pr_info("var %d\n",var);
    /* print some necessary information to let the reader know */
    
    /**
     *  Use do not exceed number of bytes to see workqueue implementation
     *  seq_file.size mean?
     */
    ret = proc_create_single_data("jiqwq", 0, NULL, jiq_read_wq, NULL);
    if (!ret) {
        pr_warn("register jiqwq proc error\n");
    }
    ret = proc_create_single_data("jiqwqdelay", 0, NULL, jiq_read_wq_delayed, NULL);
    if (!ret) {
        pr_warn("register jiqwqdelay proc error\n");
    }
    


    ret = proc_create_single_data("jitimer", 0, NULL, jiq_read_run_timer, NULL);
    if (!ret) {
        pr_warn("register jitimer proc error\n");
    }
    ret = proc_create_single_data("jiqtasklet", 0, NULL, jiq_read_tasklet, NULL);
    if (!ret) {
        pr_warn("register jiqtasklet proc error\n");
    }

    return 0;
}


static void __exit jiq_exit(void)
{
    remove_proc_entry("jiqwq", NULL);
    remove_proc_entry("jiqwqdelay", NULL);

    remove_proc_entry("jitimer", NULL);
    remove_proc_entry("jiqtasklet", NULL);
}

module_init(jiq_init);
module_exit(jiq_exit);