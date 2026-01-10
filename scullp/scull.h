
/*
 * scull.h -- definitions for the char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: scull.h,v 1.15 2004/11/04 17:51:18 rubini Exp $
 */

#ifndef _SCULL_H_
#define _SCULL_H_

#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */

/*
 * Macros to help debugging
 */

#undef PDEBUG             /* undef it, just in case */
#ifdef SCULLP_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "scull: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */

#define SCULLP_MAJOR 0   /* dynamic major by default */

#define SCULLP_DEVS  4    /* scullc0 through scullc3 */

/*
 * The bare device is a variable-length region of memory.
 * Use a linked list of indirect blocks.
 *
 * "scull_dev->data" points to an array of pointers, each
 * pointer refers to a memory area of SCULL_QUANTUM bytes.
 *
 * The array (quantum-set) is SCULL_QSET long.
 */

#define SCULLP_ORDER    0 /* one page at a time */
#define SCULLP_QSET     500

struct scullp_dev {
	void **data;
	struct scullp_dev *next;  /* next listitem */
	int vmas;                 /* active mappings */
	int order;                /* the current allocation order */
	int qset;                 /* the current array size */
	size_t size;              /* 32-bit will suffice */
	struct semaphore sem;     /* Mutual exclusion */
	struct cdev cdev;
};

extern struct scullp_dev *scullp_devices;

extern struct file_operations scullp_fops;
/*
 * Split minors in two parts
 */
#define TYPE(minor)	(((minor) >> 4) & 0xf)	/* high nibble */
#define NUM(minor)	((minor) & 0xf)		/* low  nibble */


/*
 * The different configurable parameters
 */
extern int scullp_major;     /* main.c */
extern int scullp_devs;
extern int scullp_order;
extern int scullp_qset;


/*
 * Prototypes for shared functions
 */


int     scullp_trim(struct scullp_dev *dev);
struct scullp_dev *scullp_follow(struct scullp_dev *dev, int n);

/*
 * Ioctl definitions
 */

/* Use 'k' as magic number */
#define SCULLP_IOC_MAGIC  'k'
/* Please use a different 8-bit number in your code */

#define SCULLP_IOCRESET    _IO(SCULLP_IOC_MAGIC, 0)

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define SCULLP_IOCSORDER   _IOW(SCULLP_IOC_MAGIC,  1, int)
#define SCULLP_IOCSQSET    _IOW(SCULLP_IOC_MAGIC,  2, int)
#define SCULLP_IOCTORDER _IO(SCULLP_IOC_MAGIC,   3)
#define SCULLP_IOCTQSET    _IO(SCULLP_IOC_MAGIC,   4)
#define SCULLP_IOCGORDER _IOR(SCULLP_IOC_MAGIC,  5, int)
#define SCULLP_IOCGQSET    _IOR(SCULLP_IOC_MAGIC,  6, int)
#define SCULLP_IOCQORDER _IO(SCULLP_IOC_MAGIC,   7)
#define SCULLP_IOCQQSET    _IO(SCULLP_IOC_MAGIC,   8)
#define SCULLP_IOCXORDER _IOWR(SCULLP_IOC_MAGIC, 9, int)
#define SCULLP_IOCXQSET    _IOWR(SCULLP_IOC_MAGIC,10, int)
#define SCULLP_IOCHORDER _IO(SCULLP_IOC_MAGIC,  11)
#define SCULLP_IOCHQSET    _IO(SCULLP_IOC_MAGIC,  12)


#define SCULLP_IOC_MAXNR 12

#endif /* _SCULL_H_ */
