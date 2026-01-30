#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Platform-specific includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define inet_ntop InetNtopA
    #define inet_pton InetPtonA
    #define getaddrinfo getaddrinfo_win
    #define freeaddrinfo freeaddrinfo_win
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netdb.h>
    #include <errno.h>
#endif

#include "public_address_detector.h"

#define BUFFER_SIZE 1024

// Windows下需要初始化Winsock
#ifdef _WIN32
static WSADATA wsaData;
static int wsa_initialized = 0;

void init_winsock() {
    if (!wsa_initialized) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            fprintf(stderr, "WSAStartup failed\n");
            exit(EXIT_FAILURE);
        }
        wsa_initialized = 1;
    }
}
#endif

// 检查IP地址类型
static int get_ip_type(const char *ip) {
    struct sockaddr_in sa;
    struct sockaddr_in6 sa6;

    if (inet_pton(AF_INET, ip, &(sa.sin_addr)) == 1) {
        return AF_INET;  // IPv4
    } else if (inet_pton(AF_INET6, ip, &(sa6.sin6_addr)) == 1) {
        return AF_INET6; // IPv6
    } else {
        return 0;  // 无效地址
    }
}

// 创建监听socket
static int create_listening_socket(const char *local_ip, int *port) {
    int listen_fd;
    int ip_type = get_ip_type(local_ip);

    if (ip_type == AF_INET) {
        // IPv4监听
        struct sockaddr_in listen_addr;

        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
#ifdef _WIN32
            fprintf(stderr, "IPv4 socket creation failed: %d\n", WSAGetLastError());
#else
            perror("IPv4 socket creation failed");
#endif
            return -1;
        }

        // 设置SO_REUSEADDR
        int opt = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
                      (const char*)&opt,
#else
                      (const void*)&opt,
#endif
                      sizeof(opt)) < 0) {
#ifdef _WIN32
            fprintf(stderr, "setsockopt failed: %d\n", WSAGetLastError());
#else
            perror("setsockopt failed");
#endif
            close(listen_fd);
            return -1;
        }

        // 绑定到指定IPv4地址
        memset(&listen_addr, 0, sizeof(listen_addr));
        listen_addr.sin_family = AF_INET;
        listen_addr.sin_port = 0;  // 让系统选择端口

        if (strcmp(local_ip, "0.0.0.0") == 0) {
            listen_addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, local_ip, &listen_addr.sin_addr) != 1) {
                close(listen_fd);
                return -1;
            }
        }

        if (bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
#ifdef _WIN32
            fprintf(stderr, "IPv4 bind failed: %d\n", WSAGetLastError());
#else
            perror("IPv4 bind failed");
#endif
            close(listen_fd);
            return -1;
        }

        // 获取分配的端口
        socklen_t addr_len = sizeof(listen_addr);
        if (getsockname(listen_fd, (struct sockaddr *)&listen_addr, &addr_len) == 0) {
            *port = ntohs(listen_addr.sin_port);
        } else {
#ifdef _WIN32
            fprintf(stderr, "getsockname failed: %d\n", WSAGetLastError());
#else
            perror("getsockname failed");
#endif
            close(listen_fd);
            return -1;
        }

    } else if (ip_type == AF_INET6) {
        // IPv6监听
        struct sockaddr_in6 listen_addr6;

        listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (listen_fd < 0) {
#ifdef _WIN32
            fprintf(stderr, "IPv6 socket creation failed: %d\n", WSAGetLastError());
#else
            perror("IPv6 socket creation failed");
#endif
            return -1;
        }

        // 设置SO_REUSEADDR
        int opt = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
                      (const char*)&opt,
#else
                      (const void*)&opt,
#endif
                      sizeof(opt)) < 0) {
#ifdef _WIN32
            fprintf(stderr, "setsockopt failed: %d\n", WSAGetLastError());
#else
            perror("setsockopt failed");
#endif
            close(listen_fd);
            return -1;
        }

        // 允许IPv4和IPv6都连接（可选）
        int ipv6only = 0;
        setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY,
#ifdef _WIN32
                  (const char*)&ipv6only,
#else
                  (const void*)&ipv6only,
#endif
                  sizeof(ipv6only));

        // 绑定到指定IPv6地址
        memset(&listen_addr6, 0, sizeof(listen_addr6));
        listen_addr6.sin6_family = AF_INET6;
        listen_addr6.sin6_port = 0;  // 让系统选择端口

        if (strcmp(local_ip, "::") == 0 || strcmp(local_ip, "0:0:0:0:0:0:0:0") == 0) {
            listen_addr6.sin6_addr = in6addr_any;
        } else {
            if (inet_pton(AF_INET6, local_ip, &listen_addr6.sin6_addr) != 1) {
                close(listen_fd);
                return -1;
            }
        }

        if (bind(listen_fd, (struct sockaddr *)&listen_addr6, sizeof(listen_addr6)) < 0) {
#ifdef _WIN32
            fprintf(stderr, "IPv6 bind failed: %d\n", WSAGetLastError());
#else
            perror("IPv6 bind failed");
#endif
            close(listen_fd);
            return -1;
        }

        // 获取分配的端口
        socklen_t addr_len = sizeof(listen_addr6);
        if (getsockname(listen_fd, (struct sockaddr *)&listen_addr6, &addr_len) == 0) {
            *port = ntohs(listen_addr6.sin6_port);
        } else {
#ifdef _WIN32
            fprintf(stderr, "getsockname failed: %d\n", WSAGetLastError());
#else
            perror("getsockname failed");
#endif
            close(listen_fd);
            return -1;
        }

    } else {
        fprintf(stderr, "Invalid IP address: %s\n", local_ip);
        return -1;
    }

    // 开始监听
    if (listen(listen_fd, 1) < 0) {
#ifdef _WIN32
        fprintf(stderr, "Listen failed: %d\n", WSAGetLastError());
#else
        perror("Listen failed");
#endif
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

// 连接到服务器
static int connect_to_server(const char *server_ip, int server_port) {
    int sockfd = -1;
    struct addrinfo hints, *result, *rp;
    char port_str[10];

    snprintf(port_str, sizeof(port_str), "%d", server_port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // 支持IPv4和IPv6
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(server_ip, port_str, &hints, &result);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(ret));
        return -1;
    }

    // 尝试所有返回的地址
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; // 连接成功
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd == -1) {
        fprintf(stderr, "Could not connect to server at %s:%d\n", server_ip, server_port);
        return -1;
    }

    return sockfd;
}

// 等待服务器连接
static int wait_for_server_connection(int listen_fd, int timeout) {
    int server_conn_fd;
    struct sockaddr_storage server_addr;
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUFFER_SIZE];

    // 设置accept超时
    fd_set read_fds;
    struct timeval tv;

    FD_ZERO(&read_fds);
    FD_SET(listen_fd, &read_fds);

    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    int select_result = select(listen_fd + 1, &read_fds, NULL, NULL, &tv);

    if (select_result < 0) {
#ifdef _WIN32
        fprintf(stderr, "select failed: %d\n", WSAGetLastError());
#else
        perror("select failed");
#endif
        return -1;
    } else if (select_result == 0) {
        return -1; // 超时
    }

    // 接受连接
    server_conn_fd = accept(listen_fd, (struct sockaddr *)&server_addr, &addr_len);
    if (server_conn_fd < 0) {
#ifdef _WIN32
        fprintf(stderr, "Accept failed: %d\n", WSAGetLastError());
#else
        perror("Accept failed");
#endif
        return -1;
    }

    // 从服务器接收随机值
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recv(server_conn_fd, buffer, BUFFER_SIZE - 1, 0);

    close(server_conn_fd);

    if (bytes_received > 0) {
        return 0; // 成功
    } else {
        return -1; // 失败
    }
}

// 初始化函数
int detector_init(void) {
#ifdef _WIN32
    init_winsock();
#endif
    return 0;
}

// 清理函数
void detector_cleanup(void) {
#ifdef _WIN32
    if (wsa_initialized) {
        WSACleanup();
        wsa_initialized = 0;
    }
#endif
}

// 主要检测函数
DetectionResult detect_public_address(const char* client_ip, const char* server_ip,
                                     int server_port, int timeout) {
    DetectionResult result = {0};
    int listening_port;
    int listen_fd = -1, server_fd = -1;

    // 在用户指定的IP地址上创建监听socket
    listen_fd = create_listening_socket(client_ip, &listening_port);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to create listening socket\n");
        result.success = 0;
        return result;
    }

    // 连接到服务器
    server_fd = connect_to_server(server_ip, server_port);
    if (server_fd < 0) {
        close(listen_fd);
        result.success = 0;
        return result;
    }

    // 发送客户端的IP和端口到服务器
    char message[BUFFER_SIZE];
    if (get_ip_type(client_ip) == AF_INET6 && client_ip[0] != '[') {
        snprintf(message, sizeof(message), "[%s]:%d", client_ip, listening_port);
    } else {
        snprintf(message, sizeof(message), "%s:%d", client_ip, listening_port);
    }

    if (send(server_fd, message, strlen(message), 0) < 0) {
#ifdef _WIN32
        fprintf(stderr, "Send failed: %d\n", WSAGetLastError());
#else
        perror("Send failed");
#endif
        close(server_fd);
        close(listen_fd);
        result.success = 0;
        return result;
    }

    // 接收服务器的初始响应
    char response[BUFFER_SIZE];
    memset(response, 0, sizeof(response));
    int bytes_received = recv(server_fd, response, sizeof(response) - 1, 0);

    if (bytes_received > 0) {
        response[bytes_received] = '\0';

        if (strstr(response, "SUCCESS") != NULL) {
            // 等待服务器连接并发送随机值
            if (wait_for_server_connection(listen_fd, timeout) == 0) {
                result.success = 1;
            } else {
                result.success = 0;
            }
        } else {
            result.success = 0;
        }
    } else {
        result.success = 0;
    }

    // 清理
    if (server_fd >= 0) close(server_fd);
    if (listen_fd >= 0) close(listen_fd);

    return result;
}


//gcc -fPIC -shared -o libpublic_address_detector.so public_address_detector.c -I. -lpthread