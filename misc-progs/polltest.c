/**
 *  polltest.c : file test device poll 
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <poll.h>

#define errExit(msg) do { perror(msg); exit(EXIT_FAILURE);}\
                     while(0)

int main(int argc, char ** argv)
{
    int nfds, num_open_fds;
    int i;
    struct pollfd *pfds;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s file...\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    num_open_fds = nfds = argc - 1;
    pfds = calloc(nfds, sizeof(struct pollfd));
    if (pfds == NULL)
        errExit("malloc");
    
    /* open each file on the command line and add it to pfds array */
    for (i = 0; i < nfds; i++) {
        pfds[i].fd = open(argv[i + 1], O_RDONLY);
        if (pfds[i].fd < 0)
            errExit("open");
        printf("Opened \"%s\" on fd %d\n", argv[i + 1], pfds[i].fd);
        pfds[i].events = POLLIN;
    }

    /* keep calling poll as long as at least one file descriptor is open to read */
    while (num_open_fds > 0)
    {
        int ready;
        printf("About to poll() \n");
        ready = poll(pfds, nfds, -1); /* infinite wait */
        if (ready < 0)
            errExit("poll");
        printf("Ready: %d\n", ready);

        /* Deal with array returned by poll() */
        for (i = 0; i < nfds; i++ ){
            char buf[10];
            if (pfds[i].revents != 0) {
                printf("fd= %d; events: %s%s%s\n", pfds[i].fd,
                (pfds[i].revents & POLLIN) ? "POLLIN" : "",
                (pfds[i].revents & POLLHUP) ? "POLLHUP" : "",
                (pfds[i].revents & POLLERR) ? "POLLERR" : "");
            
                if (pfds[i].revents & POLLIN) {
                    ssize_t s = read(pfds[i].fd, buf, sizeof(buf));
                    if (s < 0)
                        errExit("read");
                    printf(" read %zd bytes: %.*s\n",s, (int)s, buf);/* size + string address */
                } else {
                    printf(" closing fd %d\n", pfds[i].fd);
                    if (close(pfds[i].fd) < 0)
                        errExit("close");
                    num_open_fds--;
                }
            }
        } 
    }
    printf("All file descriptors cloesd; bye\n"); 
    exit(EXIT_SUCCESS);
}