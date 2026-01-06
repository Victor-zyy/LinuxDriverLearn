/**
 *  jit.c -- the just in time module
 * 
 * 
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>

#include <linux/fs.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/seq_file.h> // seq_file stack
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>


/**
 * This moudle is a silly one, it only embeds short code fragments
 * that show how time delays can be handled in the kernel.
 */

int delay = HZ;
module_param(delay, int, 0);

MODULE_LICENSE("GPL");

/* use these as data pointers, to implement four files in one function */
enum jit_files{
    JIT_BUSY,
    JIT_SCHED,
    JIT_QUEUE,
    JIT_SCHEDTO
};

/**
 * This function prints one line of data, after sleeping one second
 * It can sleep in different ways, according to the data pointer.
 */
int jit_fn(struct seq_file *seq, void *m)
{
    unsigned long j0, j1; /* jiffies */
    wait_queue_head_t wait;
    init_waitqueue_head(&wait);
    j0 = jiffies;
    j1 = jiffies + delay;
    
    switch ((long)m)
    {
        case JIT_BUSY:
            while (time_before(jiffies, j1))
            {
                cpu_relax();
            }
            break;
        case JIT_SCHED:
            while (time_before(jiffies, j1))
            {
                schedule();
            }
            break;
        case JIT_QUEUE:
            wait_event_interruptible_timeout(wait, 0, delay);
            break;
        case JIT_SCHEDTO:
            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout(delay);
            break;
    }
    j1 = jiffies; /* Actually value after we delayed */
    seq_printf(seq, "%9li %9li\n", j0, j1);
    return 0;
}


/**
 * This file, on the other hand, returns the current time forever.
 */
int jit_currentime(struct seq_file *seq, void *data)
{
    struct timeval tv1;
    struct timespec tv2;
    unsigned long j1;
    u64 j2;

    /* get them four */
    j1 = jiffies;
    j2 = get_jiffies_64();
    do_gettimeofday(&tv1);   // query the hardware
    tv2 = current_kernel_time(); // update the value when the timer tick occurs
    
    /* print */
    seq_printf(seq, "0x%08lx 0x%016Lx %10i.%06i\n" 
                "%40i.%09i\n",
            j1, j2,
            (int)tv1.tv_sec, (int)tv1.tv_usec,
            (int)tv2.tv_sec, (int)tv2.tv_nsec);
    return 0;
}


/**
 * The timer example follows
 */
int tdelay = 10;
module_param(tdelay, int, 0);

/* This data structure used as "data" for the timer and tasklet functions */
struct jit_data {
    struct timer_list timer;
    struct tasklet_struct tlet;
    int hi; /* tasklet or tasklet_hi */
    wait_queue_head_t wait;
    unsigned long prevjiffies;
    unsigned char *buf;
    struct seq_file *seq;
    int loops;
};

#define JIT_ASYNC_LOOPS 5

void jit_timer_fn(struct timer_list *t)
{
    struct jit_data *data = from_timer(data, t, timer);
    unsigned long j = jiffies;
    seq_printf(data->seq, "%9li  %3li    %i    %6li  %i  %s\n",
                        j, j - data->prevjiffies, in_interrupt() ? 1 : 0,
                    current->pid, smp_processor_id(), current->comm);
    if (--data->loops) {
        data->timer.expires += tdelay;
        data->prevjiffies = j;
        add_timer(&data->timer);
    } else {
        wake_up_interruptible(&data->wait);
    }
}


/* The /proc function : allocate everything to allow concurrency  */
int jit_timer(struct seq_file *seq, void *m)
{
    struct jit_data *data;
    unsigned long j = jiffies;

    data = kmalloc(sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    
    timer_setup(&data->timer, jit_timer_fn, 0);
    init_waitqueue_head(&data->wait);

    /* write the first lines in the buffe */
    seq_printf(seq, "   time   delta  inirq    pid    cpu command\n");
    seq_printf(seq, "%9li  %3li    %i    %6i   %i    %s\n",
                    j, 0L, in_interrupt() ? 1 : 0,
                current->pid, smp_processor_id(), current->comm);
    /* fill the data for our timer function */
    data->prevjiffies = j;
    data->loops = JIT_ASYNC_LOOPS;

    /* register timer */
    data->seq = seq;
    data->timer.function = jit_timer_fn;
    data->timer.expires = j + tdelay;
    add_timer(&data->timer);

    /* wait for the buffer to fill */
    wait_event_interruptible(data->wait, !data->loops);
    if (signal_pending(current))
        return -ERESTARTSYS;
    return 0;
}

void jit_tasklet_fn(unsigned long arg)
{
    struct jit_data *data = (struct jit_data *)arg;
    unsigned long j = jiffies;
    seq_printf(data->seq, "%9li  %3li    %i    %6li  %i  %s\n",
                        j, j - data->prevjiffies, in_interrupt() ? 1 : 0,
                    current->pid, smp_processor_id(), current->comm);
    if (--data->loops) {
        data->prevjiffies = j;
        if (data->hi)
            tasklet_hi_schedule(&data->tlet);
        else
            tasklet_schedule(&data->tlet);
    } else {
        wake_up_interruptible(&data->wait);
    }
}


/* the /proc function: allocate everything to allow concurrency */
int jit_tasklet(struct seq_file *seq, void *m)
{
    struct jit_data *data;
    unsigned long j = jiffies;
    long hi = (long)m;

    data = kmalloc(sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    
    init_waitqueue_head(&data->wait);

    /* write the first lines in the buffe */
    seq_printf(seq, "   time   delta  inirq    pid    cpu command\n");
    seq_printf(seq, "%9li  %3li    %i    %6i   %i    %s\n",
                    j, 0L, in_interrupt() ? 1 : 0,
                current->pid, smp_processor_id(), current->comm);
    /* fill the data for our timer function */
    data->prevjiffies = j;
    data->seq = seq;
    data->loops = JIT_ASYNC_LOOPS;

    /* register tasklet */
    tasklet_init(&data->tlet, jit_tasklet_fn, (unsigned long)data);
    data->hi = hi;
    if (hi)
        tasklet_hi_schedule(&data->tlet);
    else 
        tasklet_schedule(&data->tlet);

    /* wait for the buffer to fill */
    wait_event_interruptible(data->wait, !data->loops);
    if (signal_pending(current))
        return -ERESTARTSYS;
    return 0;
}


static int __init jit_init(void)
{
    struct proc_dir_entry *ret; 
    /* print some necessary information to let the reader know */
    pr_info("HZ : %d delay : %d\n", HZ, delay);
    ret = proc_create_single_data("currentime", 0, NULL, jit_currentime, NULL);
    if (!ret) {
        pr_warn("register currentime proc error\n");
    }
    ret = proc_create_single_data("jitbusy", 0, NULL, jit_fn, (void *)JIT_BUSY);
    if (!ret) {
        pr_warn("register jitbusy proc error\n");
    }
    ret = proc_create_single_data("jitsched", 0, NULL, jit_fn, (void *)JIT_SCHED);
    if (!ret) {
        pr_warn("register jitsched proc error\n");
    }
    ret = proc_create_single_data("jitqueue", 0, NULL, jit_fn, (void *)JIT_QUEUE);
    if (!ret) {
        pr_warn("register jitqueue proc error\n");
    }
    ret = proc_create_single_data("jitschedto", 0, NULL, jit_fn, (void *)JIT_SCHEDTO);
    if (!ret) {
        pr_warn("register jitschedto proc error\n");
    }



    ret = proc_create_single_data("jitimer", 0, NULL, jit_timer, NULL);
    if (!ret) {
        pr_warn("register jitimer proc error\n");
    }
    ret = proc_create_single_data("jitasklet", 0, NULL, jit_tasklet, NULL);
    if (!ret) {
        pr_warn("register jitasklet proc error\n");
    }
    ret = proc_create_single_data("jitasklethi", 0, NULL, jit_tasklet, (void *)1);
    if (!ret) {
        pr_warn("register jitasklethi proc error\n");
    }
    /**
     * If we don't implement the return code, some error will occur accidentally
     */
    return 0;
}

static void __exit jit_exit(void)
{
    remove_proc_entry("currentime", NULL);
    remove_proc_entry("jitbusy", NULL);
    remove_proc_entry("jitsched", NULL);
    remove_proc_entry("jitqueue", NULL);
    remove_proc_entry("jitschedto", NULL);

    remove_proc_entry("jitimer", NULL);
    remove_proc_entry("jitasklet", NULL);
    remove_proc_entry("jitasklethi", NULL);
}


module_init(jit_init);
module_exit(jit_exit);