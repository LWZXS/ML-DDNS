#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>

// 平台特定的头文件
#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    #define sleep(seconds) Sleep((seconds) * 1000)
#else
    #include <unistd.h>
    #include <signal.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
#endif

// 假设这些头文件存在
#include "libs/public_address_detector.h"
#include "libs/libcloudflare_ddns.h"

#define DETECTION_INTERVAL 30  // 30秒

typedef struct {
    char server_ip[64];
    int server_port;
    int timeout;
    char client_ip[64];
} AppConfig;

// 检测结果枚举
typedef enum {
    DETECT_UNKNOWN = -2,      // 未知错误
    DETECT_NETWORK_ERROR = -1, // 网络错误
    DETECT_FAILED = 0,        // 检测失败
    DETECT_SUCCESS = 1        // 检测成功
} DetectResult;

// 日志函数 - 改进版本，确保后台也能写入
void log_message(const char* message, ...) {
    FILE* log_file = fopen("ddns_monitor.log", "a");
    if (!log_file) {
        // 如果打开失败，尝试在当前目录创建
        log_file = fopen("ddns_monitor.log", "w");
        if (!log_file) {
            // 如果还失败，尝试输出到标准错误（如果可用）
            fprintf(stderr, "无法打开日志文件\n");
            return;
        }
    }

    // 获取当前时间
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log_file, "[%s] ", timestamp);

    // 处理可变参数
    va_list args;
    va_start(args, message);
    vfprintf(log_file, message, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);  // 立即刷新，避免缓冲区数据丢失
    fclose(log_file);
}

// DDNS更新函数
int update_ddns_config(AppConfig* config) {
    log_message("开始获取DDNS配置...");

    DDNSResult* result = RunCloudflareDDNS();
    if (!result) {
        log_message("DDNS更新失败: 返回空结果");
        return 0;
    }

    // 保存配置信息
    strncpy(config->server_ip, result->serverIP, sizeof(config->server_ip) - 1);
    config->server_port = result->serverPort;
    config->timeout = result->timeout;
    strncpy(config->client_ip, result->ipAddr, sizeof(config->client_ip) - 1);

    log_message("DDNS配置获取成功: %s:%d, 超时:%d秒, IP:%s",
                config->server_ip, config->server_port,
                config->timeout, config->client_ip);

    FreeDDNSResult(result);
    return 1;
}

// 改进的检测函数，返回详细状态
DetectResult run_detection(AppConfig* config) {
    log_message("开始网络检测: %s -> %s:%d",
                config->client_ip, config->server_ip, config->server_port);

    // 尝试初始化检测库
    int init_result = detector_init();
    if (init_result != 0) {
        // 根据错误码判断可能的原因
        log_message("检测库初始化失败，错误码: %d", init_result);

        // 常见的错误码判断（根据你的实际情况调整）
        if (init_result == -1 || init_result == -100) {
            return DETECT_NETWORK_ERROR;  // 网络相关错误
        } else if (init_result == -2) {
            return DETECT_UNKNOWN;        // 未知错误
        }
        return DETECT_UNKNOWN;
    }

    // 执行检测
    DetectionResult result = detect_public_address(
        config->client_ip,
        config->server_ip,
        config->server_port,
        config->timeout
    );

    // 清理库
    detector_cleanup();

    if (result.success) {
        log_message("检测成功");
        return DETECT_SUCCESS;
    } else {
        log_message("检测失败");

        // 这里可以根据实际需要检查 result 中是否有更多错误信息
        // 假设 result 可能包含错误码字段
        // 例如：if (result.error_code == -100) return DETECT_NETWORK_ERROR;

        return DETECT_FAILED;
    }
}

// 处理网络错误
void handle_network_error(int error_count) {
    log_message("检测到网络问题，等待恢复... (错误次数: %d)", error_count);

    // 网络错误时等待更长时间
    if (error_count < 3) {
        sleep(DETECTION_INTERVAL);  // 正常等待
    } else if (error_count < 5) {
        log_message("网络问题持续，延长等待时间...");
        sleep(DETECTION_INTERVAL * 2);  // 2倍等待
    } else {
        log_message("网络问题严重，等待5分钟...");
        sleep(300);  // 5分钟
    }
}

int main() {
    AppConfig config = {0};
    int network_error_count = 0;

    printf("DDNS监控服务启动...\n");
    printf("日志文件: ddns_monitor.log\n");

    // 记录启动时间
    log_message("========== 服务启动 ==========");

    // 1. 获取初始配置
    if (!update_ddns_config(&config)) {
        fprintf(stderr, "初始DDNS配置获取失败\n");
        log_message("服务启动失败: 无法获取DDNS配置");
        return 1;
    }

    printf("配置获取成功:\n");
    printf("  服务器: %s:%d\n", config.server_ip, config.server_port);
    printf("  超时: %d秒\n", config.timeout);
    printf("  客户端IP: %s\n", config.client_ip);
    printf("  检测间隔: %d秒\n", DETECTION_INTERVAL);

#ifndef _WIN32
    // Unix/Linux/Mac后台运行
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork失败");
        log_message("创建子进程失败: %s", strerror(errno));
        return 1;
    }

    if (pid > 0) {
        // 父进程退出
        printf("主程序退出，后台服务已启动 (PID: %d)\n", pid);
        printf("使用命令停止服务: kill %d\n", pid);
        printf("查看日志: tail -f ddns_monitor.log\n");
        log_message("主进程退出，后台进程PID: %d", pid);
        return 0;
    }

    // 子进程继续执行（后台服务）

    // 创建新会话，脱离终端
    setsid();

    // 注意：不改变工作目录，保持当前目录以便写入日志
    // chdir("/");  // 注释掉这行，保持当前目录

    // 关闭标准文件描述符，但保留错误输出
    // 只关闭标准输入，保留输出和错误以便调试
    close(STDIN_FILENO);

    // 可以打开/dev/null作为标准输入
    open("/dev/null", O_RDONLY);

    // 注意：我们不关闭STDOUT和STDERR，让后台进程也能看到输出
    // 如果需要完全后台，可以重定向到日志文件
    /*
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
    */
#endif

    log_message("后台检测服务开始运行");

    // 2. 立即执行第一次检测
    DetectResult first_result = run_detection(&config);
    switch (first_result) {
        case DETECT_SUCCESS:
            log_message("首次检测成功");
            network_error_count = 0;
            break;
        case DETECT_FAILED:
            log_message("首次检测失败，尝试重新获取配置");
            update_ddns_config(&config);
            network_error_count = 0;
            break;
        case DETECT_NETWORK_ERROR:
            log_message("首次检测发现网络错误");
            network_error_count++;
            handle_network_error(network_error_count);
            break;
        case DETECT_UNKNOWN:
            log_message("首次检测遇到未知错误");
            break;
    }

    // 3. 主循环
    int detection_count = 1;  // 从1开始，因为已经执行了一次
    while (1) {
        sleep(DETECTION_INTERVAL);
        detection_count++;

        log_message("执行第%d次检测...", detection_count);

        DetectResult result = run_detection(&config);

        switch (result) {
            case DETECT_SUCCESS:
                log_message("第%d次检测成功", detection_count);
                network_error_count = 0;  // 重置网络错误计数
                break;

            case DETECT_FAILED:
                log_message("第%d次检测失败，重新获取配置", detection_count);
                update_ddns_config(&config);
                network_error_count = 0;  // 重置网络错误计数
                break;

            case DETECT_NETWORK_ERROR:
                log_message("第%d次检测发现网络错误", detection_count);
                network_error_count++;
                handle_network_error(network_error_count);
                // 网络错误时也尝试更新配置
                log_message("网络错误，尝试更新配置...");
                update_ddns_config(&config);
                break;

            case DETECT_UNKNOWN:
                log_message("第%d次检测遇到未知错误", detection_count);
                // 未知错误也尝试更新配置
                update_ddns_config(&config);
                break;
        }

        // 每10次检测输出一次状态摘要
        if (detection_count % 10 == 0) {
            log_message("状态摘要: 已执行%d次检测，网络错误计数: %d",
                        detection_count, network_error_count);
        }
    }

    log_message("========== 服务停止 ==========");
    return 0;
}

// C语言版本的执行入口，调用lib里面的cloudflare_ddns.go进行cf的dns设置和获取系统公网IP，调用public_address_detector.c进行检查
// 编译命令
// gcc -o main main.c -I./libs -L./libs -lpublic_address_detector -lcloudflare_ddns
// 环境变量
// export DYLD_LIBRARY_PATH=./libs:.