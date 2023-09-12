#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <linux/if.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "etherip.h"
#include "tap.h"
#include "socket.h"

static pthread_t threads[THREAD_COUNT];
static pthread_barrier_t barrier;

struct etherip_hdr {
    uint8_t hdr_1st;
    uint8_t hdr_2nd;
};

struct recv_handlar_args {
    int domain;
    int sock_fd;
    int tap_fd;
    struct sockaddr dst_addr;
};

struct send_handlar_args {
    int sock_fd;
    int tap_fd;
    struct sockaddr dst_addr;
};

static void on_signal(int s){
    (void)s;
    pthread_kill(threads[0], SIGHUP);
    pthread_kill(threads[1], SIGHUP);
}

static void print_usage(){
    printf("Usage\n");
    printf("    etherip [OPTIONS] { ipv4 | ipv6 } dst <ip addr> src <ip addr> tap <tap if name> &\n");
    printf("OPTIONS\n");
    printf("    dst <ip addr>\t: set the destination ip address\n");
    printf("    src <ip addr>\t: set the source ip address\n");
    printf("    tap <tap if name>\t: set the tap IF name\n");
    printf("    --mtu <mtu>\t\t: set mtu (Not a tunnel IF mtu). default: 1500\n");

}

static void *recv_handlar(void *args){
    // setup
    int domain = ((struct recv_handlar_args *)args)->domain;
    int sock_fd = ((struct recv_handlar_args *)args)->sock_fd;
    int tap_fd = ((struct recv_handlar_args *)args)->tap_fd;
    struct sockaddr dst_addr = ((struct recv_handlar_args *)args)->dst_addr;
    
    // end setup
    pthread_barrier_wait(&barrier);

    while(1){
        ssize_t rlen, slen;
        uint8_t buffer[BUFFER_SIZE];
        struct sockaddr addr;
        socklen_t addr_len;

        rlen = sock_read(sock_fd, buffer, sizeof(buffer), &addr, &addr_len);
        if(rlen == -1){
            // Failed to sock_read()
            return NULL;
        }

        // destination check
        if(domain == AF_INET){
            struct sockaddr_in *dst_addr4;
            dst_addr4 = (struct sockaddr_in *)&dst_addr;
            struct sockaddr_in *addr4;
            addr4 = (struct sockaddr_in *)&addr;
            if(addr4->sin_addr.s_addr != dst_addr4->sin_addr.s_addr){
                continue;
            }
        }
        else if(domain == AF_INET6){
            struct sockaddr_in6 *dst_addr6;
            dst_addr6 = (struct sockaddr_in6 *)&dst_addr;
            struct sockaddr_in6 *addr6;
            addr6 = (struct sockaddr_in6 *)&addr;
            if(addr6->sin6_addr.s6_addr != dst_addr6->sin6_addr.s6_addr){
                continue;
            }
        }

        struct iphdr *ip_hdr;
        ip_hdr = (struct iphdr *)buffer;
        int ip_hdr_len = ip_hdr->ihl * 4;

        struct etherip_hdr *hdr;
        hdr = (struct etherip_hdr *)(buffer + ip_hdr_len);

        // version check
        uint8_t version = hdr->hdr_1st >> 4;
        if(version != ETHERIP_VERSION){
            // unknown version
            printf("unknown version\n");
            continue;
        }
        // reserved field check
        uint8_t reserved1 = hdr->hdr_1st & 0xF;
        uint8_t reserved2 = hdr->hdr_2nd;
        if(reserved1 != 0 || reserved2 != 0){
            // reserved field is not 0
            printf("reserved field is not 0\n");
            continue;
        }

        slen = tap_write(tap_fd, hdr+ETHERIP_HEADER_LEN, rlen);
        if(slen == -1){
            // Failed to tap_write()
            return NULL;
        }
        // need to improve
        if(slen != rlen){
            // Failed to tap_write()
            return NULL;
        }

    }

    return NULL;
}

static void *send_handlar(void *args){
    // setup
    int sock_fd = ((struct send_handlar_args *)args)->sock_fd;
    int tap_fd = ((struct send_handlar_args *)args)->tap_fd;
    struct sockaddr dst_addr = ((struct send_handlar_args *)args)->dst_addr;
    // end setup
    pthread_barrier_wait(&barrier);

    while(1){

        ssize_t rlen, slen; // receive len, send len
        uint8_t buffer[BUFFER_SIZE];

        rlen = tap_read(tap_fd, buffer, sizeof(buffer));
        if(rlen == -1){
            // Failed to tap_read()
            return NULL;
        }

        struct etherip_hdr *hdr;
        hdr = (struct etherip_hdr *)buffer;
        hdr->hdr_1st = ETHERIP_VERSION << 4;
        hdr->hdr_2nd = 0;
        memcpy(hdr+1, &buffer, rlen);
        slen = sock_write(sock_fd, buffer, sizeof(hdr)+rlen, &dst_addr, sizeof(dst_addr));
        if(slen == -1){
            // Failed to sock_write()
            return NULL;
        }
        if((unsigned long)slen != sizeof(hdr)+rlen){
            // Failed to sock_write()
            return NULL;
        }

    }

    return NULL;
}

int main(int argc, char **argv){
    signal(SIGINT, on_signal);

    if(argc == 1){
        print_usage();
        return 0;
    }

    int domain;
    char src[IPv6_ADDR_STR_LEN];
    char dst[IPv6_ADDR_STR_LEN];
    char tap_name[IFNAMSIZ];
    int mtu = 1500;
    int tap_fd;
    int sock_fd;
    

    // parse arguments
    int required_arg_cnt = 0;
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "ipv4") == 0){
            required_arg_cnt++;
            domain = AF_INET;
        }    
        if(strcmp(argv[i], "ipv6") == 0){
            required_arg_cnt++;
            domain = AF_INET6;
        }
        if(strcmp(argv[i], "dst") == 0){
            required_arg_cnt++;
            strcpy(dst, argv[++i]);
        }
        if(strcmp(argv[i], "src") == 0){
            required_arg_cnt++;
            strcpy(src, argv[++i]);
        }
        if(strcmp(argv[i], "tap") == 0){
            required_arg_cnt++;
            strcpy(tap_name, argv[++i]);
        }
        if(strcmp(argv[i], "--mtu") == 0){
            mtu = atoi(argv[++i]);
        }
        if(strcmp(argv[i], "-h") == 0){
            print_usage();
            return 0;
        }
    }
    if(required_arg_cnt != 4){
        printf("Too few or too many arguments required.\n");
        printf("Help: etherip -h\n");
        return 0;
    }

    // init
    if(tap_open(&tap_fd, tap_name, mtu) == -1){
        // Failed to tap_open()
        return 0;
    }

    struct sockaddr src_addr;
    if(domain == AF_INET){
        struct sockaddr_in *src_addr4;
        src_addr4 = (struct sockaddr_in *)&src_addr;

        src_addr4->sin_family = AF_INET;
        inet_pton(AF_INET, src, &src_addr4->sin_addr.s_addr);
        src_addr4->sin_port  = htons(ETHERIP_PROTO_NUM);
    }
    else if(domain == AF_INET6){
        struct sockaddr_in6 *src_addr6;
        src_addr6 = (struct sockaddr_in6 *)&src_addr;

        src_addr6->sin6_family = AF_INET6;
        inet_pton(AF_INET6, src, &src_addr6->sin6_addr.s6_addr);
        src_addr6->sin6_port  = htons(ETHERIP_PROTO_NUM);
    }
    socklen_t sock_len;
    sock_len = sizeof(src_addr);
    
    if(sock_open(&sock_fd, domain, &src_addr, sock_len) == -1){
        // Failed to sock_open()
        return 0;
    }
    
    struct sockaddr dst_addr;
    if(domain == AF_INET){
        struct sockaddr_in *dst_addr4;
        dst_addr4 = (struct sockaddr_in *)&dst_addr;

        dst_addr4->sin_family = AF_INET;
        inet_pton(AF_INET, dst, &dst_addr4->sin_addr.s_addr);
        dst_addr4->sin_port  = htons(ETHERIP_PROTO_NUM);
    }
    else if(domain == AF_INET6){
        struct sockaddr_in6 *dst_addr6;
        dst_addr6 = (struct sockaddr_in6 *)&dst_addr;

        dst_addr6->sin6_family = AF_INET6;
        inet_pton(AF_INET6, dst, &dst_addr6->sin6_addr.s6_addr);
        dst_addr6->sin6_port  = htons(ETHERIP_PROTO_NUM);
    }

    // start threads
    pthread_barrier_init(&barrier, NULL, 2);

    struct recv_handlar_args recv_args = {domain, sock_fd, tap_fd, dst_addr};
    pthread_create(&threads[0], NULL, recv_handlar, &recv_args);
    struct send_handlar_args send_args = {sock_fd, tap_fd, dst_addr};
    pthread_create(&threads[1], NULL, send_handlar, &send_args);

    if(pthread_join(threads[0], NULL) == 0){
        printf("Stopped recv_handlar\n");
    }
    if(pthread_join(threads[1], NULL) == 0){
        printf("Stopped send_handlar\n");
    }

    pthread_barrier_destroy(&barrier);

    // cleanup
    sock_close(sock_fd);
    tap_close(tap_fd);

    return 0;
}