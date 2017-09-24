#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "util.h"

const int BUFSZ = 256;
char buf[BUFSZ];
int read_line() {
    fgets(buf, BUFSZ, stdin);
    int n = strlen(buf);
    if (buf[n - 1] == '\n') {
        buf[--n] = 0;
    }
    return n;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s [ip] [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) perror_exit();

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_aton(server_ip, &server_addr.sin_addr) == 0) myerror_exit("wrong IPv4 address");
    if (connect(server_sockfd, (sockaddr*)&server_addr, sizeof(sockaddr_in)) == -1) perror_exit();

    while (true) { // login loop
        int fd = server_sockfd;

        printf("Enter id: ");
        fflush(stdout);
        int uname_len = read_line();

        int pssz = sizeof(int) * 2 + uname_len;
        char *ps = (char*)malloc(pssz), *psc = ps;
        generate_int(&psc, 0);
        generate_int(&psc, uname_len);
        generate_bytes(&psc, buf, uname_len);
        if (!write_packet(fd, ps, pssz)) myerror_exit("");

        char *pr = read_packet(fd), *prc = pr;
        if (!pr) myerror_exit("");

        int prtype = consume_int(&prc);
        if (prtype == 1) {
            int status = consume_int(&prc);
            if (status == 0) {
                int uid = consume_int(&prc);
                int unread = consume_int(&prc);
                printf("login success (uid = %d, # of unread msg = %d)\n", uid, unread);
                break;
            } else if (status == 1) {
                printf("login fail, try again!\n");
            } else {
                free(pr);
                fprintf(stderr, "Unknown status (status = %d)\n", status);
                myerror_exit("");
            }
        } else {
            free(pr);
            fprintf(stderr, "Unknown request type (prtype = %d)\n", prtype);
            myerror_exit("");
        }
    }

    close(server_sockfd);

    return 0;
}
