#pragma once

#include <errno.h>
#include <unistd.h>
#include <string.h>

#define errno_perror_exit(e) \
    do {\
        fprintf(stderr, "[%s:%d] ", __FILE__, __LINE__);\
        errno = e;\
        perror(NULL);\
        exit(EXIT_FAILURE);\
    } while (false)

#define perror_exit() \
    do {\
        fprintf(stderr, "[%s:%d] ", __FILE__, __LINE__);\
        perror(NULL);\
        exit(EXIT_FAILURE);\
    } while (false)

#define myerror_exit(s) \
    do {\
        fprintf(stderr, "[%s:%d] %s\n", __FILE__, __LINE__, s);\
        exit(EXIT_FAILURE);\
    } while (false)

bool myread(int fd, void *_buf, size_t count) {
    char *buf = (char*)_buf;
    do {
        ssize_t ret = read(fd, buf, count);
        if (ret == 0) break;
        if (ret == -1 && errno != EINTR) perror_exit();
        buf += ret;
        count -= ret;
    } while (count > 0);
    return count == 0;
}

bool mywrite(int fd, void *_buf, size_t count) {
    char *buf = (char*)_buf;
    do {
        ssize_t ret = write(fd, buf, count);
        if (ret == 0) break;
        if (ret == -1 && errno != EINTR) perror_exit();
        buf += ret;
        count -= ret;
    } while (count > 0);
    return count == 0;
}

char* read_packet(int fd) {
    int sz;
    if (!myread(fd, &sz, sizeof(int))) {
        return NULL;
    }
    char *packet = (char*)malloc(sz);
    if (!myread(fd, packet, sz)) {
        free(packet);
        return NULL;
    }
    return packet;
}

bool write_packet(int fd, char *packet, int sz) {
    bool ret = mywrite(fd, &sz, sizeof(int)) && mywrite(fd, packet, sz);
    free(packet);
    return ret;
}

char* consume_bytes(char* *packet, int n) {
    char *bytes = (char*)malloc(n);
    memcpy(bytes, *packet, n);
    *packet += n;
    return bytes;
}

int consume_int(char* *packet) {
    int x = *(int*)*packet;
    *packet += sizeof(int);
    return x;
}

void generate_bytes(char* *packet, char *bytes, int n) {
    memcpy(*packet, bytes, n);
    *packet += n;
}

void generate_int(char* *packet, int x) {
    *(int*)*packet = x;
    *packet += sizeof(int);
}

int find_uid_by_uname(char *uname, int len) {
    if (strncmp(uname, "A", len) == 0) return 0;
    if (strncmp(uname, "B", len) == 0) return 1;
    if (strncmp(uname, "C", len) == 0) return 2;
    if (strncmp(uname, "D", len) == 0) return 3;
    return -1;
}

const char* find_uname_by_uid(int uid) {
    if (uid == 0) return "A";
    if (uid == 1) return "B";
    if (uid == 2) return "C";
    if (uid == 3) return "D";
    return "?";
}
