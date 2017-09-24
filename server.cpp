#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include "util.h"

struct thread_info {
    pthread_t tid;
    int tnum;
    int client_sockfd;
};

void process_packets(thread_info *tinfo) {
    int fd = tinfo->client_sockfd;
    while (true) { // login loop
        // packet received
        char *pr = read_packet(fd), *prc = pr;
        if (!pr) return;

        // check packet type
        int prtype = consume_int(&prc);
        if (prtype == 0) { // login request
            int uname_len = consume_int(&prc);
            char *uname = consume_bytes(&prc, uname_len);
            int uid = find_uid_by_uname(uname, uname_len);
            free(uname);
            free(pr);
            if (uid != -1) { // success (uname found)
                int pssz = sizeof(int) * 4;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 1);
                generate_int(&psc, 0);
                generate_int(&psc, uid);
                generate_int(&psc, 42); // TODO unread count
                if (!write_packet(fd, ps, pssz)) return;
                break;
            } else { // fail (uname not found)
                int pssz = sizeof(int) * 2;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 1);
                generate_int(&psc, 1);
                if (!write_packet(fd, ps, pssz)) return;
            }
        } else {
            free(pr);
            fprintf(stderr, "Wrong request type (prtype = %d)\n", prtype);
            return;
        }
    }
}

void* handle_client(void *arg) {
    thread_info *tinfo = (thread_info*)arg;

    fprintf(stderr, "Thread (tnum = %d) is created.\n", tinfo->tnum);

    process_packets(tinfo);

    close(tinfo->client_sockfd);
    free(tinfo);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int server_port = atoi(argv[1]);

    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) perror_exit();

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sockfd, (sockaddr*)&server_addr, sizeof(sockaddr_in)) == -1) perror_exit();

    const int server_backlog = 5;
    if (listen(server_sockfd, server_backlog)) perror_exit();

    for (int tnum = 0; ; ++tnum) {
        sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(sockaddr_in);
        int client_sockfd = accept(server_sockfd, (sockaddr*)&client_addr, &client_addrlen);
        if (client_sockfd == -1) perror_exit();

        thread_info *tinfo = (thread_info*)malloc(sizeof(thread_info));
        tinfo->tnum = tnum;
        tinfo->client_sockfd = client_sockfd;
        int pthread_create_ret = pthread_create(&tinfo->tid, NULL, handle_client, tinfo);
        if (pthread_create_ret != 0) errno_perror_exit(pthread_create_ret);
    }

    close(server_sockfd);

    return 0;
}
