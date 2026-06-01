#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_FDS 65536
#define BUF_SIZE 4096
#define MAX_BACKENDS 16

struct conn {
    int tcp_fd;
    int uds_fd;
    int epoll_fd;
    char buf[BUF_SIZE];
    int len;
    int off;
    int active;
};

static struct conn* conn_by_fd[MAX_FDS];
static int backends[MAX_BACKENDS];
static int num_backends;
static int rr_idx;

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int create_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) { perror("socket"); return -1; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(fd); return -1; }
    if (listen(fd, 4096) < 0) { perror("listen"); close(fd); return -1; }
    return fd;
}

static int connect_uds(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    set_nonblock(fd);
    return fd;
}

static void close_conn(struct conn* c) {
    if (!c || !c->active) return;
    c->active = 0;
    if (c->tcp_fd >= 0 && conn_by_fd[c->tcp_fd] == c) {
        conn_by_fd[c->tcp_fd] = NULL;
        epoll_ctl(c->epoll_fd, EPOLL_CTL_DEL, c->tcp_fd, NULL);
        close(c->tcp_fd);
    }
    if (c->uds_fd >= 0 && conn_by_fd[c->uds_fd] == c) {
        conn_by_fd[c->uds_fd] = NULL;
        epoll_ctl(c->epoll_fd, EPOLL_CTL_DEL, c->uds_fd, NULL);
        close(c->uds_fd);
    }
    free(c);
}

static int flush_buf(struct conn* c, int dst_fd) {
    while (c->off < c->len) {
        int n = write(dst_fd, c->buf + c->off, c->len - c->off);
        if (n > 0) {
            c->off += n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLRDHUP, .data.ptr = c };
            epoll_ctl(c->epoll_fd, EPOLL_CTL_MOD, dst_fd, &ev);
            return 0;
        } else {
            return -1;
        }
    }
    c->len = 0;
    c->off = 0;
    return 1;
}

static int handle_read(struct conn* c, int src_fd, int dst_fd) {
    if (c->len > 0) {
        int ret = flush_buf(c, dst_fd);
        if (ret < 0) return -1;
        if (c->len > 0) return 0;
    }
    int n = read(src_fd, c->buf, BUF_SIZE);
    if (n > 0) {
        c->len = n;
        c->off = 0;
        return flush_buf(c, dst_fd);
    } else if (n == 0) {
        return -1;
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    } else {
        return -1;
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <port> <uds_path1> <uds_path2> [<uds_pathN>...]\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    num_backends = argc - 2;
    for (int i = 0; i < num_backends; i++)
        backends[i] = i + 2;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { perror("epoll_create1"); return 1; }

    int listener = create_listener(port);
    if (listener < 0) return 1;

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = listener };
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener, &ev);

    struct epoll_event events[4096];

    for (;;) {
        int nfds = epoll_wait(epoll_fd, events, 4096, -1);
        if (nfds < 0) { if (errno == EINTR) continue; break; }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == listener) {
                for (;;) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int tcp_fd = accept(listener, (struct sockaddr*)&client_addr, &client_len);
                    if (tcp_fd >= 0) set_nonblock(tcp_fd);
                    if (tcp_fd < 0) break;

                    const char* uds_path = argv[backends[rr_idx % num_backends]];
                    rr_idx++;

                    int uds_fd = connect_uds(uds_path);
                    if (uds_fd < 0) {
                        close(tcp_fd);
                        continue;
                    }

                    struct conn* c = calloc(1, sizeof(struct conn));
                    if (!c) { close(tcp_fd); close(uds_fd); continue; }
                    c->tcp_fd = tcp_fd;
                    c->uds_fd = uds_fd;
                    c->epoll_fd = epoll_fd;
                    c->active = 1;
                    conn_by_fd[tcp_fd] = c;
                    conn_by_fd[uds_fd] = c;

                    struct epoll_event ev_tcp = { .events = EPOLLIN | EPOLLRDHUP, .data.fd = tcp_fd };
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &ev_tcp);
                    struct epoll_event ev_uds = { .events = EPOLLIN | EPOLLRDHUP, .data.fd = uds_fd };
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, uds_fd, &ev_uds);
                }
            } else {
                struct conn* c = conn_by_fd[fd];
                if (!c) continue;

                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    close_conn(c);
                    continue;
                }

                int other_fd = (fd == c->tcp_fd) ? c->uds_fd : c->tcp_fd;

                if (events[i].events & EPOLLOUT) {
                    int ret = flush_buf(c, fd);
                    if (ret < 0) { close_conn(c); continue; }
                    if (c->len == 0 && c->off == 0) {
                        struct epoll_event mod = { .events = EPOLLIN | EPOLLRDHUP, .data.fd = fd };
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &mod);
                    }
                }

                if (events[i].events & EPOLLIN) {
                    int ret = handle_read(c, fd, other_fd);
                    if (ret < 0) { close_conn(c); continue; }
                }
            }
        }
    }

    close(listener);
    close(epoll_fd);
    return 0;
}
