#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <unistd.h>

#include "socket.h"
#include "etherip.h"

extern int sock_open(int *fd, int domain, struct sockaddr_storage *addr, socklen_t addr_len){
    *fd = socket(domain, SOCK_RAW, ETHERIP_PROTO_NUM);
    if(*fd == -1){
        fprintf(stderr, "[ERROR]: Failed to open socket: %s\n", strerror(errno));
        return -1;
    }

    if(domain == AF_INET){
        int pmtu_discover = IP_PMTUDISC_DONT;
        if(setsockopt(*fd, IPPROTO_IP, IP_MTU_DISCOVER, &pmtu_discover, sizeof(pmtu_discover)) == -1){
            fprintf(stderr, "[ERROR]: Failed to disable IPv4 PMTU discovery: %s\n", strerror(errno));
            close(*fd);
            return -1;
        }
    }

    // Note: By disabling PMTU discovery for IPv6, the encapsulated packets will be fragmented 1280bytes even if the underlay MTU is larger. 
    // if(domain == AF_INET6){
    //     int pmtu6 = IPV6_PMTUDISC_DONT;
    //     if(setsockopt(*fd, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &pmtu6, sizeof(pmtu6)) == -1){
    //         fprintf(stderr, "[ERROR]: Failed to disable IPv6 PMTU discovery: %s\n", strerror(errno));
    //         close(*fd);
    //         return -1;
    //     }
    // }

    if(bind(*fd, (struct sockaddr *)addr, addr_len) == -1){
        fprintf(stderr, "[ERROR]: Failed to bind socket: %s\n", strerror(errno));
        close(*fd);
        return -1;
    }
    return 0;
}

extern int sock_close(int fd){
    close(fd);
    return 0;
}

extern ssize_t sock_read(int fd, uint8_t *buffer, size_t size, struct sockaddr_storage *addr, socklen_t *addr_len){
    ssize_t len;
    len = recvfrom(fd, buffer, size, 0, (struct sockaddr *)addr, addr_len);
    if(len <= 0){
        fprintf(stderr, "[ERROR]: sock_read: %s\n", strerror(errno));
        return -1;
    }
    return len;
}

extern ssize_t sock_write(int fd, const uint8_t *frame, size_t size, struct sockaddr_storage *addr, socklen_t addr_len){
    ssize_t len;
    len = sendto(fd, frame, size, 0, (struct sockaddr *)addr, addr_len);
    if(len <= 0){
        fprintf(stderr, "[ERROR]: sock_write: %s\n", strerror(errno));
        return -1;
    }
    return len;
}
