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
#include "memory.h"

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
    struct sockaddr_storage *dst_addr;
};

struct send_handlar_args {
    int domain;
    int sock_fd;
    int tap_fd;
    struct sockaddr_storage *dst_addr;
};

struct tap_writer_args {
    int domain;
    int tap_fd;
    struct sockaddr_storage dst_addr;
    struct sockaddr_storage src_addr;
    ssize_t rlen;
    uint8_t buffer[];
};

struct sock_writer_args {
    int domain;
    int sock_fd;
    struct sockaddr_storage dst_addr;
    size_t rlen;
    uint8_t buffer[];
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

static void *tap_writer(void *args){

    pthread_detach(pthread_self());

    struct sockaddr_storage *src_addr = &((struct tap_writer_args *)args)->src_addr;
    struct sockaddr_storage *dst_addr = &((struct tap_writer_args *)args)->dst_addr;
    uint8_t *buffer = ((struct tap_writer_args *)args)->buffer;
    ssize_t *rlen = &((struct tap_writer_args *)args)->rlen;

    uint8_t reserved1;
    uint8_t reserved2;
    struct iphdr *ip_hdr;
    int ip_hdr_len;
    struct etherip_hdr *hdr;
    uint8_t version;
    size_t write_len;

    if(((struct tap_writer_args *)args)->domain == AF_INET){

        /* packet size check */
        if((size_t)*rlen < sizeof(struct iphdr) + sizeof(struct etherip_hdr)){
            // too short
            memory_free((struct tap_writer_args *)args);
            pthread_exit((void *) 0);
            return NULL;
        }

        // destination check
        struct sockaddr_in *dst_addr4;
        dst_addr4 = (struct sockaddr_in *)dst_addr;
        struct sockaddr_in *addr4;
        addr4 = (struct sockaddr_in *)src_addr;
        if(addr4->sin_addr.s_addr != dst_addr4->sin_addr.s_addr){
            memory_free((struct tap_writer_args *)args);
            pthread_exit((void *) 0);
            return NULL;
        }

        // skip header
        ip_hdr = (struct iphdr *)buffer;
        ip_hdr_len = ip_hdr->ihl * 4;
        hdr = (struct etherip_hdr *)(buffer + ip_hdr_len);
        write_len = *rlen - ETHERIP_HEADER_LEN - ip_hdr_len;
    }
    else if(((struct tap_writer_args *)args)->domain == AF_INET6){

        /* packet size check */
        if((size_t)*rlen < sizeof(struct ip6_hdr) + sizeof(struct etherip_hdr)){
            // too short
            memory_free((struct tap_writer_args *)args);
            pthread_exit((void *) 0);
            return NULL;
        }

        // destination check
        struct sockaddr_in6 *dst_addr6;
        dst_addr6 = (struct sockaddr_in6 *)dst_addr;
        struct sockaddr_in6 *addr6;
        addr6 = (struct sockaddr_in6 *)src_addr;
        if(memcmp(addr6->sin6_addr.s6_addr, dst_addr6->sin6_addr.s6_addr, sizeof(addr6->sin6_addr.s6_addr)) != 0){
            memory_free((struct tap_writer_args *)args);
            pthread_exit((void *) 0);
            return NULL;
        }

        hdr = (struct etherip_hdr *)(&buffer);
        write_len = *rlen - ETHERIP_HEADER_LEN;
    }


    // version check
    version = hdr->hdr_1st >> 4;
    if(version != ETHERIP_VERSION){
        // unknown version
        memory_free((struct tap_writer_args *)args);
        pthread_exit((void *) 0);
        return NULL;
    }
    // reserved field check
    reserved1 = hdr->hdr_1st & 0xF;
    reserved2 = hdr->hdr_2nd;
    if(reserved1 != 0 || reserved2 != 0){
        // reserved field is not 0
        memory_free((struct tap_writer_args *)args);
        pthread_exit((void *) 0);
        return NULL;
    }

    tap_write(((struct tap_writer_args *)args)->tap_fd, (uint8_t *)(hdr+1), write_len);
    memory_free((struct tap_writer_args *)args);
    return NULL;
}

static void *sock_writer(void *args){
    uint8_t *buffer = ((struct sock_writer_args *)args)->buffer;

    uint8_t frame[BUFFER_SIZE];
    struct etherip_hdr *hdr;
    size_t dst_addr_len;

    hdr = (struct etherip_hdr *)frame;
    hdr->hdr_1st = ETHERIP_VERSION << 4;
    hdr->hdr_2nd = 0;
    memcpy(hdr+1, buffer, ((struct sock_writer_args *)args)->rlen);
    if(((struct sock_writer_args *)args)->domain == AF_INET)
        dst_addr_len = sizeof( *(struct sockaddr_in *) &((struct sock_writer_args *)args)->dst_addr );
    else if(((struct sock_writer_args *)args)->domain == AF_INET6){
        dst_addr_len = sizeof( *(struct sockaddr_in6 *) &((struct sock_writer_args *)args)->dst_addr );
    }

    sock_write(((struct sock_writer_args *)args)->sock_fd, frame, sizeof(struct etherip_hdr) + ((struct sock_writer_args *)args)->rlen, &((struct sock_writer_args *)args)->dst_addr, dst_addr_len);
    memory_free((struct sock_writer_args *)args);
    return NULL;
}

static void *recv_handlar(void *args){
    // setup
    int domain = ((struct recv_handlar_args *)args)->domain;
    int sock_fd = ((struct recv_handlar_args *)args)->sock_fd;
    int tap_fd = ((struct recv_handlar_args *)args)->tap_fd;
    struct sockaddr_storage *dst_addr = ((struct recv_handlar_args *)args)->dst_addr;
    
    ssize_t rlen;
    uint8_t buffer[BUFFER_SIZE];
    struct sockaddr_storage addr;

    pthread_t tap_writer_thread;
    // end setup
    pthread_barrier_wait(&barrier);

    while(1){

        rlen = sock_read(sock_fd, buffer, sizeof(buffer), &addr, NULL);
        if(rlen == -1){
            // Failed to sock_read()
            return NULL;
        }

        struct tap_writer_args *tap_writer_args;
        tap_writer_args = memory_alloc(sizeof(struct tap_writer_args)+rlen);
        tap_writer_args->domain = domain;
        tap_writer_args->tap_fd = tap_fd;
        tap_writer_args->dst_addr = *dst_addr;
        tap_writer_args->src_addr = addr;
        tap_writer_args->rlen = rlen;
        memcpy(tap_writer_args->buffer, buffer, rlen);
        pthread_create(&tap_writer_thread, NULL, tap_writer, tap_writer_args);
    }

    return NULL;
}

static void *send_handlar(void *args){
    // setup
    int domain = ((struct send_handlar_args *)args)->domain;
    int sock_fd = ((struct send_handlar_args *)args)->sock_fd;
    int tap_fd = ((struct send_handlar_args *)args)->tap_fd;
    struct sockaddr_storage *dst_addr = ((struct send_handlar_args *)args)->dst_addr;
    ssize_t rlen; // receive len
    uint8_t buffer[BUFFER_SIZE];

    pthread_t sock_writer_thread;
    // end setup
    pthread_barrier_wait(&barrier);

    while(1){

        rlen = tap_read(tap_fd, buffer, sizeof(buffer));
        if(rlen == -1){
            // Failed to tap_read()
            return NULL;
        }

        struct sock_writer_args *sock_writer_args;
        sock_writer_args = memory_alloc(sizeof(struct sock_writer_args)+rlen);
        sock_writer_args->domain = domain;
        sock_writer_args->sock_fd = sock_fd;
        sock_writer_args->dst_addr = *dst_addr;
        sock_writer_args->rlen = rlen;
        memcpy(sock_writer_args->buffer, buffer, rlen);
        pthread_create(&sock_writer_thread, NULL, sock_writer, sock_writer_args);
    }

    return NULL;
}

int main(int argc, char **argv){
    int domain;
    char src[IPv6_ADDR_STR_LEN];
    char dst[IPv6_ADDR_STR_LEN];
    char tap_name[IFNAMSIZ];
    int mtu = 1500;
    int tap_fd;
    int sock_fd;
    int required_arg_cnt;

    signal(SIGINT, on_signal);

    if(argc == 1){
        print_usage();
        return 0;
    }
    
    /* parse arguments */
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
        printf("[ERROR]: Too few or too many arguments required.\n");
        printf("Help: etherip -h\n");
        return 0;
    }

    /* open tap */
    if(tap_open(&tap_fd, tap_name, mtu, domain) == -1){
        // Failed to tap_open()
        return 0;
    }

    /* prepare src_addr */
    struct sockaddr_storage src_addr;
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
    
    /* open socket */
    if(sock_open(&sock_fd, domain, &src_addr, sock_len) == -1){
        // Failed to sock_open()
        return 0;
    }
    
    /* prepare dst_addr */
    struct sockaddr_storage dst_addr;
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
    struct send_handlar_args send_args = {domain, sock_fd, tap_fd, &dst_addr};
    pthread_create(&threads[1], NULL, send_handlar, &send_args);

    fprintf(stdout, "[INFO]: Started etherip. dst: %s src: %s\n", dst, src);

    if(pthread_join(threads[0], NULL) == 0){
        fprintf(stderr, "[ERROR]: Stopped recv_handlar\n");
        pthread_kill(threads[1], SIGHUP);
        fprintf(stderr, "[ERROR]: Stopped etherip\n");
    }
    if(pthread_join(threads[1], NULL) == 0){
        fprintf(stderr, "[ERROR]: Stopped send_handlar\n");
        pthread_kill(threads[0], SIGHUP);
        fprintf(stderr, "[ERROR]: Stopped etherip\n");
    }

    pthread_barrier_destroy(&barrier);

    // cleanup
    sock_close(sock_fd);
    tap_close(tap_fd);

    return 0;
}