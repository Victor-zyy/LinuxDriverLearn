/**
 * 
 *  pipeasync.c : use async nodification to read scullpipe0 device
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

int gotdata = 0;

void sighandler(int signo)
{
    if (signo == SIGIO)
        gotdata++;
}


char buffer[4096];

int main(int argc, char **argv)
{
    int count;
    int fd;
    int flags;
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = sighandler;
    action.sa_flags = 0;

    sigaction(SIGIO, &action, NULL);

    /**
     * Two steps to setup
     */
    fd = open("/dev/scullpipe0", O_RDONLY); 
    if (fd < 0) {
        perror("open device error: ");
        exit(EXIT_FAILURE);
    }
    printf("open file fd %d\n", fd);
    fcntl(fd, F_SETOWN, getpid()); /* filp->f_owner and nothing happened */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | FASYNC | O_NONBLOCK); /* FASYNC driver method */
    /**
     *  By default: the io is blocking
     */
    while (1)
    {
        /* this only returns if a signal arrives */
        /**
         *sleep() function does not automatically 
         *resume after a signal is handled because of 
         *how system calls and library functions are designed 
         *in POSIX-compliant operating systems like Linux.  
         */
        sleep(8000);
        if (!gotdata)
            continue;
        count = read(fd, buffer, 4096);
        if (count < 0) /* maybe error we are not Blocking IO by default */
            printf("count %d\n", count);
        printf("read from pipedevice async method: "); /* need to notice that glic buffer 4096 orso then outputs using the write system call*/
        write(1, buffer, count);
        gotdata = 0;
    }
}

