#pragma once

typedef int (*route_handler_t)(const char* body, char* resp, int resp_sz);

void register_route(const char* method, const char* path, route_handler_t handler);
int run_server(const char* sock_path);
int run_server_tcp(int port);
