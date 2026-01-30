#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <netdb.h>

#define BUFFER_SIZE 1024
#define DEFAULT_PORT 8066
#define MAX_CLIENTS 10
#define TIMEOUT_SEC 5

void generate_random_string(char *buffer, size_t length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t i;
    
    srand(time(NULL));
    for (i = 0; i < length - 1; i++) {
        buffer[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    buffer[length - 1] = '\0';
}

int is_valid_ipv6(const char *ip) {
    struct sockaddr_in6 sa;
    return inet_pton(AF_INET6, ip, &(sa.sin6_addr)) != 0;
}

int is_valid_ipv4(const char *ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) != 0;
}

int parse_client_address(const char *input, char *ip, int *port) {
    char buffer[BUFFER_SIZE];
    strncpy(buffer, input, BUFFER_SIZE - 1);
    buffer[BUFFER_SIZE - 1] = '\0';
    
    printf("Parsing address: %s\n", buffer);
    
    // 检查是否是IPv6地址（包含中括号）
    if (buffer[0] == '[') {
        // IPv6地址格式: [IPv6]:port
        char *close_bracket = strchr(buffer, ']');
        if (!close_bracket) {
            return -1; // 没有闭合括号
        }
        
        if (close_bracket[1] != ':') {
            return -1; // 格式错误，应该是]:port
        }
        
        // 提取IPv6地址（去掉括号）
        size_t ip_len = close_bracket - buffer - 1;
        if (ip_len >= INET6_ADDRSTRLEN) {
            return -1;
        }
        strncpy(ip, buffer + 1, ip_len);
        ip[ip_len] = '\0';
        
        // 提取端口号
        *port = atoi(close_bracket + 2);
        
        printf("Parsed IPv6: %s, port: %d\n", ip, *port);
    } else {
        // IPv4地址格式: ip:port
        char *colon = strrchr(buffer, ':');
        if (!colon) {
            return -1; // 没有冒号
        }
        
        // 提取IPv4地址
        size_t ip_len = colon - buffer;
        if (ip_len >= INET6_ADDRSTRLEN) {
            return -1;
        }
        strncpy(ip, buffer, ip_len);
        ip[ip_len] = '\0';
        
        // 提取端口号
        *port = atoi(colon + 1);
        
        printf("Parsed IPv4: %s, port: %d\n", ip, *port);
    }
    
    return 0;
}

int connect_to_client(const char *ip, int port) {
    int sockfd;
    struct addrinfo hints, *result, *rp;
    char port_str[10];
    
    printf("Attempting to connect to %s:%d\n", ip, port);
    
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // 支持IPv4和IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = IPPROTO_TCP;
    
    int ret = getaddrinfo(ip, port_str, &hints, &result);
    if (ret != 0) {
        printf("getaddrinfo error: %s\n", gai_strerror(ret));
        return -1;
    }
    
    // 尝试所有返回的地址
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        
        // 设置超时
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT_SEC;
        timeout.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break; // 连接成功
        }
        
        close(sockfd);
    }
    
    freeaddrinfo(result);
    
    if (rp == NULL) {
        printf("Could not connect to any address\n");
        return -1;
    }
    
    printf("Successfully connected to %s:%d\n", ip, port);
    return sockfd;
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    // 初始化随机数种子
    srand(time(NULL));
    
    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(DEFAULT_PORT);
    
    // 设置SO_REUSEADDR选项
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // 绑定socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // 监听连接
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d\n", DEFAULT_PORT);
    printf("Waiting for client connections...\n\n");
    
    while (1) {
        // 接受客户端连接
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Client connected from: %s:%d\n", client_ip, ntohs(client_addr.sin_port));
        
        // 接收客户端发送的IP和端口
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("Received from client: %s\n", buffer);
            
            // 解析客户端地址和端口
            char client_target_ip[INET6_ADDRSTRLEN];
            int client_target_port;
            
            if (parse_client_address(buffer, client_target_ip, &client_target_port) < 0) {
                printf("Failed to parse client address\n");
                send(client_fd, "ERROR: Invalid address format", 28, 0);
            } else {
                // 尝试连接客户端指定的地址
                int target_fd = connect_to_client(client_target_ip, client_target_port);
                
                if (target_fd >= 0) {
                    printf("Successfully connected to client's address\n");
                    
                    // 生成并发送随机值
                    char random_value[33];
                    generate_random_string(random_value, sizeof(random_value));
                    printf("Sending random value: %s\n", random_value);
                    
                    if (send(target_fd, random_value, strlen(random_value), 0) < 0) {
                        perror("Send to target failed");
                        send(client_fd, "ERROR: Failed to send random value", 34, 0);
                    } else {
                        send(client_fd, "SUCCESS: Random value sent", 26, 0);
                    }
                    
                    close(target_fd);
                } else {
                    printf("Failed to connect to client's address\n");
                    send(client_fd, "ERROR: Cannot connect to specified address", 42, 0);
                }
            }
        } else {
            printf("No data received from client\n");
        }
        
        close(client_fd);
        printf("Connection closed\n\n");
    }
    
    close(server_fd);
    return 0;
}

// DEFAULT_PORT监听端口
// 编译命令
// gcc -o server server.c
