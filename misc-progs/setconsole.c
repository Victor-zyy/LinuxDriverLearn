/**
 * setconsole.c -- choose a console to receive kernel message
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>


int main(int argc, char **argv)
{
    char bytes[2] = {11, 0};// 11 is the TIOCLINUX cmd number

    char *tty_name = ttyname(STDIN_FILENO); // Get the name of the input TTY
    if (tty_name) {
        printf("STDIN is connected to: %s\n", tty_name);
    } else {
        perror("ttyname error");
    }
    
    if (argc == 2) bytes[1] = atoi(argv[1]);
    else {
        fprintf(stderr, "%s: need a signle arg\n", argv[0]);
        exit(1);
    }

    if (ioctl(STDIN_FILENO, TIOCLINUX, bytes) < 0) {
        fprintf(stderr, "%s; ioctl(stdin, TIOCLINUX): %s\n",
            argv[0], strerror(errno));
        exit(1);
    }

    exit(0);
}
