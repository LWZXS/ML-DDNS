#ifndef PUBLIC_ADDRESS_DETECTOR_H
#define PUBLIC_ADDRESS_DETECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

// 返回结果结构体
typedef struct {
    int success;
} DetectionResult;

// 初始化函数
int detector_init(void);

// 清理函数
void detector_cleanup(void);

// 主要检测函数
DetectionResult detect_public_address(const char* client_ip, const char* server_ip, int server_port, int timeout);

#ifdef __cplusplus
}
#endif

#endif // PUBLIC_ADDRESS_DETECTOR_H