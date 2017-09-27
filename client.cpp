#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "util.h"

int fd;
const int BUFSZ = 256;
char buf[BUFSZ];

/*
 * Read a line from stdin, and save to buf without newline character.
 * Returns number of bytes read.
 */
int read_line() {
    fgets(buf, BUFSZ, stdin);
    int n = strlen(buf);
    if (buf[n - 1] == '\n') {
        buf[--n] = 0;
    }
    return n;
}

/*
 * Infinitely read user input, parse it, and send appropriate packet to server.
 */
void* handle_send(void *arg) {
    while (true) {
        int msg_len = read_line();
        if (strncmp(buf, "/invite ", 8) == 0) { // invite
            int uid = find_uid_by_uname(buf + 8, 1);
            if (uid != -1) {
                int pssz = sizeof(int) * 3;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 4);
                generate_int(&psc, 1);
                generate_int(&psc, uid);
                if (!write_packet(fd, ps, pssz)) myerror_exit("");
            } else {
                printf("No such user\n");
            }
        } else if (strncmp(buf, "/leave", 6) == 0) { // leave
            int pssz = sizeof(int) * 2;
            char *ps = (char*)malloc(pssz), *psc = ps;
            generate_int(&psc, 4);
            generate_int(&psc, 2);
            if (!write_packet(fd, ps, pssz)) myerror_exit("");
            printf("Leaving...\n");
            close(fd);
            exit(0);
        } else if (strncmp(buf, "/exit", 5) == 0) { // exit
            int pssz = sizeof(int) * 2;
            char *ps = (char*)malloc(pssz), *psc = ps;
            generate_int(&psc, 4);
            generate_int(&psc, 3);
            if (!write_packet(fd, ps, pssz)) myerror_exit("");
            printf("Exiting...\n");
            close(fd);
            exit(0);
        } else { // normal msg
            int pssz = sizeof(int) * 3 + msg_len;
            char *ps = (char*)malloc(pssz), *psc = ps;
            generate_int(&psc, 4);
            generate_int(&psc, 0);
            generate_int(&psc, msg_len);
            generate_bytes(&psc, buf, msg_len);
            if (!write_packet(fd, ps, pssz)) myerror_exit("");
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s [ip] [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // connect to server
    char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) perror_exit();

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_aton(server_ip, &server_addr.sin_addr) == 0) myerror_exit("wrong IPv4 address");
    if (connect(server_sockfd, (sockaddr*)&server_addr, sizeof(sockaddr_in)) == -1) perror_exit();

    fd = server_sockfd;
    int unread;
    while (true) { // login loop
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
            if (status == 0) { // login success
                int uid = consume_int(&prc);
                unread = consume_int(&prc);
                free(pr);
                printf("login success!\n");
                break;
            } else if (status == 1) { // login fail
                free(pr);
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

    while (unread == -1) { // group invite loop
        printf("Waiting for group invitation... ");
        fflush(stdout);

        char *pr = read_packet(fd), *prc = pr;
        if (!pr) myerror_exit("");
        int prtype = consume_int(&prc);
        if (prtype == 2) { // invited
            free(pr);

            printf("invited! y or n? ");
            fflush(stdout);
            read_line();
            if (strcmp(buf, "y") == 0) { // invitation accept
                int pssz = sizeof(int) * 2;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 3);
                generate_int(&psc, 0);
                if (!write_packet(fd, ps, pssz)) myerror_exit("");
                printf("Accepted!\n");
                break;
            } else { // invitation reject
                int pssz = sizeof(int) * 2;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 3);
                generate_int(&psc, 1);
                if (!write_packet(fd, ps, pssz)) myerror_exit("");
                printf("Rejected!\n");
            }
        } else {
            free(pr);
            fprintf(stderr, "Unknown request type (prtype = %d)\n", prtype);
            myerror_exit("");
        }
    }

    if (unread != -1) {
        printf("You have %d unread messages.\n", unread);
    }

    pthread_t handle_send_tid;
    pthread_create(&handle_send_tid, NULL, handle_send, NULL);

    while (true) { // msg send loop
        char *pr = read_packet(fd), *prc = pr;
        if (!pr) myerror_exit("");
        int prtype = consume_int(&prc);
        if (prtype == 5) {
            int status = consume_int(&prc);
            if (status == 0) { // normal msg
                int uid = consume_int(&prc);
                int msg_len = consume_int(&prc);
                char *msg = consume_bytes(&prc, msg_len);
                free(pr);
                printf("[%s] ", find_uname_by_uid(uid));
                fflush(stdout);
                write(STDOUT_FILENO, msg, msg_len);
                printf("\n");
                fflush(stdout);
                free(msg);
            } else if (status == 1) { // invitation
                int uid = consume_int(&prc);
                int invitee = consume_int(&prc);
                free(pr);
                printf("[%s invited %s]\n", find_uname_by_uid(uid), find_uname_by_uid(invitee));
            } else if (status == 2) { // someone left
                int uid = consume_int(&prc);
                free(pr);
                printf("[%s left]\n", find_uname_by_uid(uid));
            } else if (status == 3) { // someone exit
                int uid = consume_int(&prc);
                free(pr);
                printf("[%s exit]\n", find_uname_by_uid(uid));
            } else if (status == 4) { // someone accepted invitation
                int uid = consume_int(&prc);
                free(pr);
                printf("[%s accepted invitation]\n", find_uname_by_uid(uid));
            } else if (status == 5) { // someone rejected invitation
                int uid = consume_int(&prc);
                free(pr);
                printf("[%s rejected invitation]\n", find_uname_by_uid(uid));
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
