/**
 * 
 * 
 * setlevel.c -- choose a console_loglevel for the kernel
 * 
 * 
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

/* #include <unistd.h> */ /* conflicting on the alpha */
//#define __LIBRARY__ /* _syscall3 and friends are only available through this */
//#include <linux/unistd.h>



/* define the system call, to overide the library function */

/**
 * 
 * sys_syslog system call
 * 
 */
/* Close the log.  Currently a NOP. */
// #define SYSLOG_ACTION_CLOSE          0
/* Open the log. Currently a NOP. */
// #define SYSLOG_ACTION_OPEN           1
/* Read from the log. */
// #define SYSLOG_ACTION_READ           2
/* Read all messages remaining in the ring buffer. */
// #define SYSLOG_ACTION_READ_ALL       3
/* Read and clear all messages remaining in the ring buffer */
// #define SYSLOG_ACTION_READ_CLEAR     4
/* Clear ring buffer. */
// #define SYSLOG_ACTION_CLEAR          5
/* Disable printk's to console */
// #define SYSLOG_ACTION_CONSOLE_OFF    6
/* Enable printk's to console */
// #define SYSLOG_ACTION_CONSOLE_ON     7
/* Set level of messages printed to console */
// #define SYSLOG_ACTION_CONSOLE_LEVEL  8
/* Return number of unread characters in the log buffer */
// #define SYSLOG_ACTION_SIZE_UNREAD    9
/* Return size of the log buffer */
// #define SYSLOG_ACTION_SIZE_BUFFER   10
/**
 * old version for x86/arm .etc 
 * now this version is deprecated
 * new version is syscall
 */
//_syscall3(int, syslog, int, type, char *,bufp, int, len);

int main(int argc, char **argv)
{
    int level;

    if (argc == 2) {
        level = atoi(argv[1]);  /* the chosen console */
    } else {
        fprintf(stderr, "%s: need a single arg\n", argv[0]);
        exit(1);
    }

    if (syscall(__NR_syslog, 8, NULL, level) < 0) {
        fprintf(stderr, "%s: syslog(setlevel): %s\n", argv[0],
                strerror(errno));
        exit(1);
    }

    exit(0);
}
