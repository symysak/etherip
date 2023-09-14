#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <linux/if.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
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
    struct sockaddr *dst_addr;
};

struct send_handlar_args {
    int sock_fd;
    int tap_fd;
    struct sockaddr *dst_addr;
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
    struct sockaddr *dst_addr = ((struct recv_handlar_args *)args)->dst_addr;
    
    // end setup
    pthread_barrier_wait(&barrier);

    while(1){
        ssize_t rlen;
        uint8_t buffer[BUFFER_SIZE];
        struct sockaddr addr;
        socklen_t addr_len;
        uint8_t reserved1;
        uint8_t reserved2;
        struct iphdr *ip_hdr;
        int ip_hdr_len;
        struct etherip_hdr *hdr;
        uint8_t version;

        rlen = sock_read(sock_fd, buffer, sizeof(buffer), &addr, &addr_len);
        if(rlen == -1){
            // Failed to sock_read()
            return NULL;
        }

        
        if(domain == AF_INET){
            if((size_t)rlen < sizeof(struct iphdr) + sizeof(struct etherip_hdr)){
                // too short
                continue;
            }

            // destination check
            struct sockaddr_in *dst_addr4;
            dst_addr4 = (struct sockaddr_in *)dst_addr;
            struct sockaddr_in *addr4;
            addr4 = (struct sockaddr_in *)&addr;
            if(addr4->sin_addr.s_addr != dst_addr4->sin_addr.s_addr){
                continue;
            }

            // skip header
            ip_hdr = (struct iphdr *)buffer;
            ip_hdr_len = ip_hdr->ihl * 4;
            hdr = (struct etherip_hdr *)(buffer + ip_hdr_len);
        }
        else if(domain == AF_INET6){
            if((size_t)rlen < sizeof(struct ip6_hdr) + sizeof(struct etherip_hdr)){
                // too short
                continue;
            }

            // destination check
            struct sockaddr_in6 *dst_addr6;
            dst_addr6 = (struct sockaddr_in6 *)dst_addr;
            struct sockaddr_in6 *addr6;
            addr6 = (struct sockaddr_in6 *)&addr;
            if(memcmp(addr6->sin6_addr.s6_addr, dst_addr6->sin6_addr.s6_addr, sizeof(addr6->sin6_addr.s6_addr)) != 0){
	            continue;
            }

            hdr = (struct etherip_hdr *)(&buffer);
        }


        // version check
        version = hdr->hdr_1st >> 4;
        if(version != ETHERIP_VERSION){
            // unknown version
            continue;
        }
        // reserved field check
        reserved1 = hdr->hdr_1st & 0xF;
        reserved2 = hdr->hdr_2nd;
        if(reserved1 != 0 || reserved2 != 0){
            // reserved field is not 0
            continue;
        }

        tap_write(tap_fd, (uint8_t *)(hdr+1), rlen - sizeof(struct etherip_hdr) - ip_hdr_len);
    }

    return NULL;
}

static void *send_handlar(void *args){
    // setup
    int sock_fd = ((struct send_handlar_args *)args)->sock_fd;
    int tap_fd = ((struct send_handlar_args *)args)->tap_fd;
    struct sockaddr *dst_addr = ((struct send_handlar_args *)args)->dst_addr;
    size_t dst_addr_len = sizeof( *(struct sockaddr_in6 *)dst_addr );

    ssize_t rlen; // receive len
    uint8_t buffer[BUFFER_SIZE];
    uint8_t frame[BUFFER_SIZE];
    struct etherip_hdr *hdr;
    // end setup
    pthread_barrier_wait(&barrier);

    while(1){

        rlen = tap_read(tap_fd, buffer, sizeof(buffer));
        if(rlen == -1){
            // Failed to tap_read()
            return NULL;
        }

        hdr = (struct etherip_hdr *)frame;
        hdr->hdr_1st = ETHERIP_VERSION << 4;
        hdr->hdr_2nd = 0;
        memcpy(hdr+1, buffer, rlen);
        sock_write(sock_fd, frame, sizeof(struct etherip_hdr) + rlen, dst_addr, dst_addr_len);

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
    int required_arg_cnt;
    

    // parse arguments
    required_arg_cnt = 0;
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
    if(tap_open(&tap_fd, tap_name, mtu, domain) == -1){
        // Failed to tap_open()
        return 0;
    }

    struct sockaddr src_addr;
    socklen_t sock_len;
    if(domain == AF_INET){
        struct sockaddr_in *src_addr4;
        src_addr4 = (struct sockaddr_in *)&src_addr;

        src_addr4->sin_family = AF_INET;
        inet_pton(AF_INET, src, &src_addr4->sin_addr.s_addr);
        src_addr4->sin_port  = htons(ETHERIP_PROTO_NUM);
        sock_len =  sizeof(*src_addr4);
    }
    else if(domain == AF_INET6){
        struct sockaddr_in6 *src_addr6;
        src_addr6 = (struct sockaddr_in6 *)&src_addr;

        src_addr6->sin6_family = AF_INET6;
        inet_pton(AF_INET6, src, &src_addr6->sin6_addr.s6_addr);
        src_addr6->sin6_port = htons(ETHERIP_PROTO_NUM);
        sock_len = sizeof(*src_addr6);
    }
    
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
	    dst_addr6->sin6_port = htons(ETHERIP_PROTO_NUM);
    }

    // start threads
    pthread_barrier_init(&barrier, NULL, 2);

    struct recv_handlar_args recv_args = {domain, sock_fd, tap_fd, &dst_addr};
    pthread_create(&threads[0], NULL, recv_handlar, &recv_args);
    struct send_handlar_args send_args = {sock_fd, tap_fd, &dst_addr};
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