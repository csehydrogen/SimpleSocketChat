#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "util.h"

struct thread_info {
    pthread_t tid;
    int tnum;
    int client_sockfd;
};

const int MAX_USER = 4;
int user_fd[MAX_USER] = {-1, -1, -1, -1};
bool in_group[MAX_USER] = {true, false, false, false};

/*
 * Every time event happened (msg, invitation, and etc.), event is pushed into eq.
 * handle_eq thread monitors eq in Round-Robin manner, and send appropriate packet to clients.
 * eq is protected by eql lock.
 * When there is no event in eq, handle_eq thread sleeps, and eqc is used to wake up the thread.
 */
struct event {
    char *ps;
    int pssz;
    event(char *_ps, int _pssz) : ps(_ps), pssz(_pssz) {}
};
std::queue<event> eq[MAX_USER];
pthread_mutex_t eql = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t eqc = PTHREAD_COND_INITIALIZER;

/*
 * Monitors eq in Round-Robin manner.
 */
void* handle_eq(void *arg) {
    fprintf(stderr, "handle_eq thread is created.\n");

    pthread_mutex_lock(&eql);
    while (true) {
        bool flag = true;
        for (int i = 0; i < MAX_USER; ++i) {
            if (user_fd[i] != -1 && eq[i].size()) { // user i logged in and have some packets to receive
                flag = false;
                char *ps = eq[i].front().ps;
                int pssz = eq[i].front().pssz;
                eq[i].pop();
                if (!write_packet(user_fd[i], ps, pssz)) {
                    close(user_fd[i]);
                    user_fd[i] = -1;
                    fprintf(stderr, "failed to write packet (uid = %d)\n", i);
                }
            }
        }
        if (flag) {
            pthread_cond_wait(&eqc, &eql);
        }
    }
    pthread_mutex_unlock(&eql);
}

/*
 * Send packet to specific user.
 */
void send_to(int i, char *ps, int pssz) {
    pthread_mutex_lock(&eql);
    eq[i].push(event(ps, pssz));
    pthread_mutex_unlock(&eql);
    pthread_cond_signal(&eqc);
}

/*
 * Send packet to all users.
 */
void broadcast(char *ps, int pssz) {
    for (int i = 0; i < MAX_USER; ++i) {
        if (in_group[i]) {
            char *buf = (char*)malloc(pssz);
            memcpy(buf, ps, pssz);
            pthread_mutex_lock(&eql);
            eq[i].push(event(buf, pssz));
            pthread_mutex_unlock(&eql);
        }
    }
    free(ps);
    pthread_cond_signal(&eqc);
}

/*
 * Continuously receive packets from client and process.
 * There are three phases : login, group invitation accept, and msg loop.
 */
void process_packets(thread_info *tinfo) {
    int fd = tinfo->client_sockfd, uid;
    while (true) { // login loop
        // packet received
        char *pr = read_packet(fd), *prc = pr;
        if (!pr) return;

        // check packet type
        int prtype = consume_int(&prc);
        if (prtype == 0) { // login request
            int uname_len = consume_int(&prc);
            char *uname = consume_bytes(&prc, uname_len);
            uid = find_uid_by_uname(uname, uname_len);
            free(uname);
            free(pr);
            if (uid != -1) { // success (uname found)
                int pssz = sizeof(int) * 4;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 1);
                generate_int(&psc, 0);
                generate_int(&psc, uid);
                if (in_group[uid]) {
                    pthread_mutex_lock(&eql);
                    generate_int(&psc, eq[uid].size());
                    pthread_mutex_unlock(&eql);
                } else {
                    generate_int(&psc, -1);
                }
                if (!write_packet(fd, ps, pssz)) return;
                user_fd[uid] = fd;
                pthread_cond_signal(&eqc);
                fprintf(stderr, "logged in (uid = %d)\n", uid);
                break;
            } else { // fail (uname not found)
                int pssz = sizeof(int) * 2;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 1);
                generate_int(&psc, 1);
                if (!write_packet(fd, ps, pssz)) return;
                fprintf(stderr, "uname not found\n");
            }
        } else {
            free(pr);
            fprintf(stderr, "Wrong packet type (prtype = %d)\n", prtype);
            return;
        }
    }

    while (!in_group[uid]) { // group accept/reject loop
        char *pr = read_packet(fd), *prc = pr;
        if (!pr) return;

        int prtype = consume_int(&prc);
        if (prtype == 3) { // group accept/reject
            int status = consume_int(&prc);
            free(pr);
            if (status == 0) { // accept
                in_group[uid] = true;

                int pssz = sizeof(int) * 3;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 5);
                generate_int(&psc, 4);
                generate_int(&psc, uid);
                broadcast(ps, pssz);

                fprintf(stderr, "invitation accepted (uid = %d)\n", uid);
                break;
            } else if (status == 1) { // reject
                int pssz = sizeof(int) * 3;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 5);
                generate_int(&psc, 5);
                generate_int(&psc, uid);
                broadcast(ps, pssz);

                fprintf(stderr, "invitation rejected (uid = %d)\n", uid);
            } else {
                fprintf(stderr, "Wrong status (status = %d)\n", status);
                return;
            }
        } else {
            free(pr);
            fprintf(stderr, "Wrong packet type (prtype = %d)\n", prtype);
            return;
        }
    }

    while (true) { // msg loop
        char *pr = read_packet(fd), *prc = pr;
        if (!pr) return;

        int prtype = consume_int(&prc);
        if (prtype == 4) { // msg
            int status = consume_int(&prc);
            if (status == 0) { // normal msg
                int msg_len = consume_int(&prc);
                char *msg = consume_bytes(&prc, msg_len);
                free(pr);

                int pssz = sizeof(int) * 4 + msg_len;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 5);
                generate_int(&psc, 0);
                generate_int(&psc, uid);
                generate_int(&psc, msg_len);
                generate_bytes(&psc, msg, msg_len);
                free(msg);
                broadcast(ps, pssz);

                fprintf(stderr, "normal msg (uid = %d)\n", uid);
            } else if (status == 1) { // invite
                int invitee = consume_int(&prc);
                free(pr);

                {
                    int pssz = sizeof(int) * 4;
                    char *ps = (char*)malloc(pssz), *psc = ps;
                    generate_int(&psc, 5);
                    generate_int(&psc, 1);
                    generate_int(&psc, uid);
                    generate_int(&psc, invitee);
                    broadcast(ps, pssz);
                }

                {
                    int pssz = sizeof(int) * 1;
                    char *ps = (char*)malloc(pssz), *psc = ps;
                    generate_int(&psc, 2);
                    send_to(invitee, ps, pssz);
                }

                fprintf(stderr, "invite (uid = %d, invitee = %d)\n", uid, invitee);
            } else if (status == 2) { // leave
                free(pr);

                int pssz = sizeof(int) * 3;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 5);
                generate_int(&psc, 2);
                generate_int(&psc, uid);
                close(fd);
                user_fd[uid] = -1;
                in_group[uid] = false;
                pthread_mutex_lock(&eql);
                eq[uid] = std::queue<event>();
                pthread_mutex_unlock(&eql);
                broadcast(ps, pssz);
                fprintf(stderr, "leave (uid = %d)\n", uid);
                return;
            } else if (status == 3) { // exit
                free(pr);

                int pssz = sizeof(int) * 3;
                char *ps = (char*)malloc(pssz), *psc = ps;
                generate_int(&psc, 5);
                generate_int(&psc, 3);
                generate_int(&psc, uid);
                close(fd);
                user_fd[uid] = -1;
                broadcast(ps, pssz);
                fprintf(stderr, "exit (uid = %d)\n", uid);
                return;
            } else {
                free(pr);
                fprintf(stderr, "Wrong status (status = %d)\n", status);
                return;
            }
        } else {
            free(pr);
            fprintf(stderr, "Wrong packet type (prtype = %d)\n", prtype);
            return;
        }
    }
}

/*
 * Just wrapper for process_packets.
 */
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

    pthread_t handle_eq_tid;
    pthread_create(&handle_eq_tid, NULL, handle_eq, NULL);

    // accept clients and make a thread for each client
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
