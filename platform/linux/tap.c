#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/if.h>
#include <linux/if_tun.h>


#include "tap.h"

extern int tap_open(int *fd, char name[], int mtu){
    *fd = open("/dev/net/tun", O_RDWR);
    if(*fd == -1){
        fprintf(stderr, "Failed to open tap: %s\n", strerror(errno));
        return -1;
    }

    struct ifreq ifr = {};
    strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name)-1);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if(ioctl(*fd, TUNSETIFF, &ifr) == -1){
        fprintf(stderr, "Failed to TUNSETIFF: %s\n", strerror(errno));
        close(*fd);
        return -1;
    }



    ifr.ifr_mtu = mtu;
    if(ioctl(socket(AF_INET, SOCK_DGRAM, 0), SIOCSIFMTU, &ifr) == -1){
        fprintf(stderr, "Failed to SIOCSIFMTU: %s\n", strerror(errno));
        close(*fd);
        return -1;
    }
    
    return 0;
}

extern int tap_close(int fd){
    close(fd);
    return 0;
}

extern ssize_t tap_read(int fd, uint8_t *buffer, size_t size){
    ssize_t len;

    len = read(fd, buffer, size);
    if(len <= 0){
        fprintf(stderr, "tap_read: %s", strerror(errno));
        return -1;
    }
    return len;
}

extern ssize_t tap_write(int fd, const uint8_t *frame, size_t flen){
    ssize_t len;

    len = write(fd, frame, flen);
    if(len <= 0){
        fprintf(stderr, "tap_write: %s", strerror(errno));
        return -1;
    }
    return len;
}