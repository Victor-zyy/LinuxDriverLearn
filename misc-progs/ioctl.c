#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "scull.h"


int main(int argc, char **argv)
{
    int fd;
    int quantum, qset, pbuffer;
    fd = open("/dev/scull0", O_RDWR); /* read and open */
    if (fd < 0) {
        perror("open error"); /* appending error cause */
        exit(1);
    }
    ioctl(fd, SCULL_IOCGQUANTUM, &quantum); /* get by pointer */
    printf(" quantum get by pointer is %d\n", quantum);
    quantum = 0;
    quantum = ioctl(fd, SCULL_IOCQQUANTUM);
    printf(" quantum get by pointer is %d\n", quantum);

    /* Test all the ioctl commands list */
    /* 1. first with the scull_quantum and scull_qset */
    quantum = 8000;
    ioctl(fd, SCULL_IOCSQUANTUM, &quantum);
    printf(" quantum set by pointer is %d\n", ioctl(fd, SCULL_IOCQQUANTUM));
    quantum = 2048;
    ioctl(fd, SCULL_IOCTQUANTUM, quantum);
    printf(" quantum set by vaule is %d\n", ioctl(fd, SCULL_IOCQQUANTUM));

    qset = 100;
    ioctl(fd, SCULL_IOCSQSET, &qset);
    printf(" qset set by pointer is %d\n", ioctl(fd, SCULL_IOCQQSET));
    qset = 200;
    ioctl(fd, SCULL_IOCTQSET, qset);
    printf(" qset set by vaule is %d\n", ioctl(fd, SCULL_IOCQQSET));

    /* 2. exchange and shift */
    quantum = 9000; 
    ioctl(fd, SCULL_IOCXQUANTUM, &quantum);
    printf(" quantum set by exchange pointer is %d original %d\n", ioctl(fd, SCULL_IOCQQUANTUM), quantum);
    quantum = 4000; 
    quantum = ioctl(fd, SCULL_IOCHQUANTUM, quantum);
    printf(" quantum set by exchange value is %d original %d\n", ioctl(fd, SCULL_IOCQQUANTUM), quantum);
    /* 3. reset to normal */
    ioctl(fd, SCULL_IOCRESET);
    printf(" quantum reset %d qset reset %d\n", ioctl(fd, SCULL_IOCQQUANTUM), ioctl(fd, SCULL_IOCQQSET));

    /* 4. pipe buffer and stuff */
    pbuffer = ioctl(fd, SCULL_P_IOCQSIZE);
    printf(" pipe buffer size get by value is %d\n", pbuffer);
    return 0;
}