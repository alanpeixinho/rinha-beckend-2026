#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "server.h"
#include "profiler.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>

enum { BUF_SIZE = 32768, MAX_CONN = 16384, MAX_EV = 128, MAX_ROUTES = 32 };

struct Route {
    const char* method;
    int method_len;
    const char* path;
    int path_len;
    route_handler_t handler;
};

static Route routes[MAX_ROUTES];
static int num_routes;

void register_route(const char* method, const char* path, route_handler_t handler) {
    if (num_routes >= MAX_ROUTES) return;
    routes[num_routes].method = method;
    routes[num_routes].method_len = (int)strlen(method);
    routes[num_routes].path = path;
    routes[num_routes].path_len = (int)strlen(path);
    routes[num_routes].handler = handler;
    num_routes++;
}

struct Conn {
    char buf[BUF_SIZE];
    int len;
    char wbuf[BUF_SIZE];
    int wlen;
};

static Conn g_cb[MAX_CONN] __attribute__((aligned(64)));

struct ReqInfo {
    const char* method;
    int method_len;
    const char* path;
    int path_len;
    const char* body;
    int body_len;
};

static int parse_content_length(const char* hdr_end, const char* buf) {
    const char* p = buf;
    while (p < hdr_end) {
        if ((*p == 'c' || *p == 'C') && hdr_end - p > 15) {
            if ((p[1] == 'o' || p[1] == 'O') &&
                (p[2] == 'n' || p[2] == 'N') &&
                (p[3] == 't' || p[3] == 'T') &&
                (p[4] == 'e' || p[4] == 'E') &&
                (p[5] == 'n' || p[5] == 'N') &&
                (p[6] == 't' || p[6] == 'T') &&
                p[7] == '-' &&
                (p[8] == 'l' || p[8] == 'L') &&
                (p[9] == 'e' || p[9] == 'E') &&
                (p[10] == 'n' || p[10] == 'N') &&
                (p[11] == 'g' || p[11] == 'G') &&
                (p[12] == 't' || p[12] == 'T') &&
                (p[13] == 'h' || p[13] == 'H') &&
                p[14] == ':') {
                p += 15;
                while (p < hdr_end && (*p == ' ' || *p == '\t')) p++;
                int val = 0;
                while (p < hdr_end && *p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                return val;
            }
        }
        p++;
    }
    return 0;
}

static int parse_request(const char* buf, int len, struct ReqInfo* info) {
    const char* hdr_end = (const char*)memmem(buf, len, "\r\n\r\n", 4);
    if (!hdr_end) return 0;

    const char* body = hdr_end + 4;

    const char* p = buf;
    int method_len = 0;
    while (p < hdr_end && *p != ' ') { p++; method_len++; }
    if (p >= hdr_end || *p != ' ') return 0;
    info->method = buf;
    info->method_len = method_len;

    p++;
    info->path = p;
    int path_len = 0;
    while (p < hdr_end && *p != ' ') { p++; path_len++; }
    info->path_len = path_len;

    info->body = body;

    if (method_len == 3 && memcmp(buf, "GET", 3) == 0) {
        info->body_len = 0;
        return (int)(body - buf);
    }

    int content_len = parse_content_length(hdr_end, buf);
    info->body_len = content_len;
    int total = (int)(body - buf) + content_len;
    if (len < total) return 0;
    return total;
}

static char* itoa_fast(int val, char* buf) {
    char* p = buf;
    if (val == 0) { *p++ = '0'; return p; }
    char temp[16];
    int n = 0;
    while (val > 0) {
        temp[n++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = n - 1; i >= 0; i--) *p++ = temp[i];
    return p;
}

static int build_response(char* resp, int resp_sz,
                           const char* content_type, int ct_len,
                           const char* body, int body_len) {
    static const char prefix[] = "HTTP/1.1 200 OK\r\nContent-Type: ";
    static const char mid[] = "\r\nContent-Length: ";
    static const char suffix[] = "\r\nConnection: keep-alive\r\n\r\n";

    char* p = resp;
    char* end = resp + resp_sz;

    auto append = [&](const char* src, int sz) -> bool {
        if (p + sz > end) return false;
        memcpy(p, src, sz);
        p += sz;
        return true;
    };

    if (!append(prefix, sizeof(prefix) - 1)) return 0;
    if (!append(content_type, ct_len)) return 0;
    if (!append(mid, sizeof(mid) - 1)) return 0;

    char lbuf[16];
    char* lend = itoa_fast(body_len, lbuf);
    if (!append(lbuf, (int)(lend - lbuf))) return 0;

    if (!append(suffix, sizeof(suffix) - 1)) return 0;
    if (!append(body, body_len)) return 0;

    return (int)(p - resp);
}

static bool wants_close(const char* buf, const char* hdr_end) {
    const char* p = buf;
    while (p < hdr_end) {
        if (*p == '\r' && p + 1 < hdr_end && p[1] == '\n') {
            const char* line = p + 2;
            if (hdr_end - line >= 11 &&
                (line[0] == 'c' || line[0] == 'C') &&
                (line[1] == 'o' || line[1] == 'O') &&
                (line[2] == 'n' || line[2] == 'N') &&
                (line[3] == 'n' || line[3] == 'N') &&
                (line[4] == 'e' || line[4] == 'E') &&
                (line[5] == 'c' || line[5] == 'C') &&
                (line[6] == 't' || line[6] == 'T') &&
                (line[7] == 'i' || line[7] == 'I') &&
                (line[8] == 'o' || line[8] == 'O') &&
                (line[9] == 'n' || line[9] == 'N') &&
                line[10] == ':') {
                const char* v = line + 11;
                while (v < hdr_end && *v == ' ') v++;
                return (hdr_end - v >= 5 && memcmp(v, "close", 5) == 0);
            }
        }
        p++;
    }
    return false;
}

static int process_request(struct ReqInfo* info, char* resp, int resp_sz, bool* keep_alive) {
    ScopedTimer _t("process_request");
    if (keep_alive) {
        const char* hdr_end = info->body - 4;
        *keep_alive = !wants_close(info->method, hdr_end);
    }

    for (int i = 0; i < num_routes; i++) {
        if (info->method_len != routes[i].method_len) continue;
        if (info->path_len != routes[i].path_len) continue;
        if (memcmp(info->method, routes[i].method, info->method_len) != 0) continue;
        if (memcmp(info->path, routes[i].path, info->path_len) != 0) continue;

        char body_buf[4096];
        int body_len = routes[i].handler(info->body, body_buf, sizeof(body_buf));
        if (body_len <= 0) return 0;

        bool is_get = (info->method_len == 3 && memcmp(info->method, "GET", 3) == 0);
        const char* ct = is_get ? "text/plain" : "application/json";
        return build_response(resp, resp_sz, ct, is_get ? 10 : 16, body_buf, body_len);
    }

    // Default: precomputed "ok" for unmatched routes
    static const char ok_resp[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 2\r\n"
        "Connection: keep-alive\r\n\r\n"
        "ok";
    if (resp_sz < (int)sizeof(ok_resp) - 1) return 0;
    memcpy(resp, ok_resp, sizeof(ok_resp) - 1);
    return sizeof(ok_resp) - 1;
}

static int add_fd(int epfd, int fd, uint32_t events) {
    struct epoll_event ev = {.events = events, .data = {.fd = fd}};
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static int mod_fd(int epfd, int fd, uint32_t events) {
    struct epoll_event ev = {.events = events, .data = {.fd = fd}};
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

static int run_server_loop(int sfd, const char* label) {
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); close(sfd); return 1; }

    add_fd(epfd, sfd, EPOLLIN | EPOLLET);

    printf("listening on %s\n", label);

    // Warm-up: prime instruction cache and branch predictor
    {
        char warmup[256];
        struct ReqInfo wi;
        if (parse_request("GET /ready HTTP/1.1\r\n\r\n", 24, &wi) > 0)
            process_request(&wi, warmup, sizeof(warmup), nullptr);
        if (parse_request("POST /fraud-score HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}", 57, &wi) > 0)
            process_request(&wi, warmup, sizeof(warmup), nullptr);
    }

    struct epoll_event events[MAX_EV];

    while (true) {
        int n = epoll_wait(epfd, events, MAX_EV, -1);
        if (n < 0) { if (errno == EINTR) continue; perror("epoll_wait"); break; }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (ev & (EPOLLHUP | EPOLLERR)) { close(fd); continue; }

            if (fd == sfd) {
                while (true) {
                    int c = accept4(sfd, nullptr, nullptr, SOCK_NONBLOCK);
                    if (c < 0) break;
                    if (c >= MAX_CONN) { close(c); continue; }
                    g_cb[c].len = 0;
                    g_cb[c].wlen = 0;
                    add_fd(epfd, c, EPOLLIN | EPOLLET);
                }
                continue;
            }

            if (fd < 0 || fd >= MAX_CONN) { close(fd); continue; }

            Conn* cb = &g_cb[fd];
            bool err = false;

            // Flush pending writes (EPOLLOUT)
            if (cb->wlen > 0 && (ev & EPOLLOUT)) {
                int w = write(fd, cb->wbuf, cb->wlen);
                if (w < 0) {
                    if (errno != EAGAIN) err = true;
                } else if (w > 0) {
                    cb->wlen -= w;
                    if (cb->wlen > 0)
                        memmove(cb->wbuf, cb->wbuf + w, cb->wlen);
                }
                if (err) { close(fd); continue; }
                if (cb->wlen == 0)
                    mod_fd(epfd, fd, EPOLLIN | EPOLLET);
            }

            // Read new data (EPOLLIN) — always consume the edge
            if (ev & EPOLLIN) {
                while (true) {
                    int r = read(fd, cb->buf + cb->len, BUF_SIZE - cb->len - 1);
                    if (r < 0) { if (errno == EAGAIN) break; err = true; break; }
                    if (r == 0) { err = true; break; }
                    cb->len += r;
                    cb->buf[cb->len] = 0;
                }
                if (err) { close(fd); continue; }
            }

            // Process requests only when no pending writes
            while (cb->wlen == 0 && cb->len > 0) {
                struct ReqInfo info;
                int consumed = parse_request(cb->buf, cb->len, &info);
                if (consumed <= 0) {
                    if (cb->len >= BUF_SIZE - 1) err = true;
                    break;
                }

                bool keep_alive = true;
                char resp[BUF_SIZE];
                int resp_len = process_request(&info, resp, BUF_SIZE, &keep_alive);
                if (resp_len <= 0) { err = true; break; }

                int rem = cb->len - consumed;
                if (rem > 0)
                    memmove(cb->buf, cb->buf + consumed, rem);
                cb->len = rem;

                int written = 0;
                while (written < resp_len) {
                    int w = write(fd, resp + written, resp_len - written);
                    if (w < 0) {
                        if (errno == EAGAIN) {
                            int remaining = resp_len - written;
                            if (cb->wlen + remaining > BUF_SIZE) { err = true; break; }
                            memcpy(cb->wbuf + cb->wlen, resp + written, remaining);
                            cb->wlen += remaining;
                            mod_fd(epfd, fd, EPOLLIN | EPOLLOUT | EPOLLET);
                            break;
                        }
                        err = true;
                        break;
                    }
                    if (w == 0) { err = true; break; }
                    written += w;
                }

                if (err || cb->wlen > 0 || !keep_alive) break;
            }
            if (err) close(fd);
        }
    }

    close(epfd);
    close(sfd);
    if (label[0] == '/') unlink(label);
    return 0;
}

int run_server(const char* sock_path) {
    unlink(sock_path);

    int sfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sfd); return 1;
    }

    chmod(sock_path, 0666);

    if (listen(sfd, 4096) < 0) { perror("listen"); close(sfd); return 1; }

    return run_server_loop(sfd, sock_path);
}

int run_server_tcp(int port) {
    int sfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sfd); return 1;
    }
    if (listen(sfd, 4096) < 0) { perror("listen"); close(sfd); return 1; }

    char label[32];
    snprintf(label, sizeof(label), "0.0.0.0:%d", port);

    return run_server_loop(sfd, label);
}
