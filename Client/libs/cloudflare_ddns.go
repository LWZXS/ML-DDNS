package main

/*
#cgo CFLAGS: -I.
#cgo windows LDFLAGS: -L. -lpublic_address_detector
#cgo linux LDFLAGS: -L. -lpublic_address_detector -lpthread
#cgo darwin LDFLAGS: -L. -lpublic_address_detector -lpthread

#include "public_address_detector.h"
#include <stdlib.h>
// 定义结构体，与C头文件中的一致
typedef struct {
    char* result;
    char* serverIP;
    int   serverPort;
    int   timeout;
    char* ipAddr;
} DDNSResult;
*/
import "C"
import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"strings"
	"time"
	"unsafe"
)

type Config struct {
	APIKey     string `json:"apiKey"`
	Email      string `json:"email"`
	ZoneID     string `json:"zoneID"`
	Domain     string `json:"domain"`
	RecordName string `json:"recordName"`
	ServerIP   string `json:"serverIP"`
	ServerPort int    `json:"serverPort"`
	Timeout    int    `json:"timeout"`
}

type DNSRecord struct {
	ID      string `json:"id"`
	Type    string `json:"type"`
	Name    string `json:"name"`
	Content string `json:"content"`
}

type CloudflareResponse struct {
	Success  bool        `json:"success"`
	Errors   []string    `json:"errors"`
	Messages []string    `json:"messages"`
	Result   interface{} `json:"result"`
}

type DNSListResponse struct {
	Success  bool        `json:"success"`
	Errors   []string    `json:"errors"`
	Messages []string    `json:"messages"`
	Result   []DNSRecord `json:"result"`
}

var cfg Config

func init() {
	// 从配置文件读取
	data, err := ioutil.ReadFile("conf/config.json")
	if err != nil {
		// 如果文件不存在，使用默认配置
		cfg = Config{
			APIKey:     "",
			Email:      "",
			ZoneID:     "",
			Domain:     "",
			RecordName: "",
			ServerIP:   "",
			ServerPort: 0,
			Timeout:    10,
		}
		return
	}
	json.Unmarshal(data, &cfg)
}

// SetDNS 简单的封装函数
func SetDNS(ipType, ip string) string {
	if result, err := setCloudflareDNS(ipType, ip); err != nil {
		return fmt.Sprintf("错误: %v", err)
	} else {
		return result
	}
}

// 查询现有DNS记录
func getExistingDNSRecord(ipType string) (*DNSRecord, error) {
	fullName := cfg.RecordName + "." + cfg.Domain
	if cfg.RecordName == "" {
		fullName = cfg.Domain
	}

	// 使用API查询记录
	url := fmt.Sprintf("https://api.cloudflare.com/client/v4/zones/%s/dns_records?type=%s&name=%s",
		cfg.ZoneID, ipType, fullName)

	req, _ := http.NewRequest("GET", url, nil)
	req.Header.Set("Authorization", "Bearer "+cfg.APIKey)
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: time.Duration(cfg.Timeout) * time.Second}
	resp, err := client.Do(req)

	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	body, _ := ioutil.ReadAll(resp.Body)

	var response DNSListResponse
	if err := json.Unmarshal(body, &response); err != nil {
		return nil, err
	}

	if !response.Success || len(response.Result) == 0 {
		return nil, nil // 记录不存在
	}

	// 返回第一个匹配的记录
	return &response.Result[0], nil
}

func setCloudflareDNS(ipType, ip string) (string, error) {
	if ipType != "A" && ipType != "AAAA" {
		return "", fmt.Errorf("无效IP类型")
	}

	fullName := cfg.RecordName + "." + cfg.Domain
	if cfg.RecordName == "" {
		fullName = cfg.Domain
	}

	// 1. 先查询现有记录
	existingRecord, err := getExistingDNSRecord(ipType)
	if err != nil {
		return "", fmt.Errorf("查询记录失败: %v", err)
	}

	// 2. 如果记录存在且IP相同，直接返回
	if existingRecord != nil && existingRecord.Content == ip {
		return fmt.Sprintf("✅ IP未改变: %s 已经是 %s", fullName, ip), nil
	}

	// 3. 显示对比信息
	if existingRecord != nil {
		fmt.Printf("当前记录IP: %s\n", existingRecord.Content)
		fmt.Printf("要设置的IP: %s\n", ip)
	}

	var url string
	var method string
	var action string

	// 构建请求数据
	data := fmt.Sprintf(`{
		"type": "%s",
		"name": "%s",
		"content": "%s",
		"ttl": 120,
		"proxied": false
	}`, ipType, fullName, ip)

	// 4. 根据是否存在记录决定是创建还是更新
	if existingRecord != nil {
		// 更新现有记录
		url = fmt.Sprintf("https://api.cloudflare.com/client/v4/zones/%s/dns_records/%s",
			cfg.ZoneID, existingRecord.ID)
		method = "PUT"
		action = "更新"
	} else {
		// 创建新记录
		url = fmt.Sprintf("https://api.cloudflare.com/client/v4/zones/%s/dns_records", cfg.ZoneID)
		method = "POST"
		action = "创建"
	}

	req, _ := http.NewRequest(method, url, strings.NewReader(data))
	req.Header.Set("Authorization", "Bearer "+cfg.APIKey)
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: time.Duration(cfg.Timeout) * time.Second}

	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	body, _ := ioutil.ReadAll(resp.Body)

	if strings.Contains(string(body), `"success":true`) {
		return fmt.Sprintf("✅ %s成功: %s → %s", action, fullName, ip), nil
	}

	return fmt.Sprintf("❌ %s失败", action), nil
}

func GetSystemIPs() (map[string][]string, error) {
	result := map[string][]string{
		"ipv4": make([]string, 0),
		"ipv6": make([]string, 0),
	}

	interfaces, err := net.Interfaces()
	if err != nil {
		return result, err
	}

	for _, iface := range interfaces {
		// 跳过环回和非活动接口
		if iface.Flags&net.FlagLoopback != 0 || iface.Flags&net.FlagUp == 0 {
			continue
		}

		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}

		for _, addr := range addrs {
			ipNet, ok := addr.(*net.IPNet)
			if !ok {
				continue
			}

			ip := ipNet.IP

			// 处理IPv4
			if ip4 := ip.To4(); ip4 != nil {
				if !ip4.IsLoopback() && ip4.IsGlobalUnicast() {
					result["ipv4"] = append(result["ipv4"], ip4.String())
				}
			} else {
				// 处理IPv6
				if !ip.IsLoopback() && !ip.IsLinkLocalUnicast() && ip.IsGlobalUnicast() {
					result["ipv6"] = append(result["ipv6"], ip.String())
				}
			}
		}
	}

	return result, nil
}

// DetectPublicAddress 封装C库的公共地址检测函数
func DetectPublicAddress(clientIP, serverIP string, serverPort, timeout int) (int, error) {
	// 初始化库
	if C.detector_init() != 0 {
		return 0, errors.New("Failed to initialize detector library")
	}
	defer C.detector_cleanup()

	// 将Go字符串转换为C字符串
	cClientIP := C.CString(clientIP)
	cServerIP := C.CString(serverIP)

	// 确保释放C字符串内存
	defer func() {
		C.free(unsafe.Pointer(cClientIP))
		C.free(unsafe.Pointer(cServerIP))
	}()

	// 调用C库函数
	result := C.detect_public_address(
		cClientIP,
		cServerIP,
		C.int(serverPort),
		C.int(timeout),
	)

	return int(result.success), nil
}

// DetectAllIPs 检测所有IP地址
func DetectAllIPs(ips map[string][]string, serverIP string, serverPort, timeout int) (successIPs, failIPs, errorIPs [][2]string) {
	// 检测IPv4地址
	if ipv4List, exists := ips["ipv4"]; exists {
		for _, clientIP := range ipv4List {
			success, err := DetectPublicAddress(clientIP, serverIP, serverPort, timeout)

			result := [2]string{"ipv4", clientIP}

			if err != nil {
				errorIPs = append(errorIPs, result)
				log.Printf("检测IPv4地址 %s 时出错: %v\n", clientIP, err)
			} else if success == 1 {
				successIPs = append(successIPs, result)
			} else {
				failIPs = append(failIPs, result)
			}
		}
	}

	// 检测IPv6地址
	if ipv6List, exists := ips["ipv6"]; exists {
		for _, clientIP := range ipv6List {
			success, err := DetectPublicAddress(clientIP, serverIP, serverPort, timeout)

			result := [2]string{"ipv6", clientIP}

			if err != nil {
				errorIPs = append(errorIPs, result)
				log.Printf("检测IPv6地址 %s 时出错: %v\n", clientIP, err)
			} else if success == 1 {
				successIPs = append(successIPs, result)
			} else {
				failIPs = append(failIPs, result)
			}
		}
	}

	return successIPs, failIPs, errorIPs
}

// 以下是C语言可调用的接口
//
//export RunCloudflareDDNS
func RunCloudflareDDNS() *C.DDNSResult {
	// 获取系统IP
	ips, err := GetSystemIPs()
	if err != nil {
		result := (*C.DDNSResult)(C.malloc(C.size_t(unsafe.Sizeof(C.DDNSResult{}))))
		result.result = C.CString(fmt.Sprintf("获取IP失败: %v", err))
		result.serverIP = C.CString(cfg.ServerIP)
		result.serverPort = C.int(cfg.ServerPort)
		result.timeout = C.int(cfg.Timeout)
		result.ipAddr = C.CString("")
		return result
	}

	// 检测公共IP
	successIPs, _, _ := DetectAllIPs(ips, cfg.ServerIP, cfg.ServerPort, cfg.Timeout)

	// 分配结果结构体内存
	result := (*C.DDNSResult)(C.malloc(C.size_t(unsafe.Sizeof(C.DDNSResult{}))))

	// 设置服务器配置信息
	result.serverIP = C.CString(cfg.ServerIP)
	result.serverPort = C.int(cfg.ServerPort)
	result.timeout = C.int(cfg.Timeout)

	if len(successIPs) > 0 {
		var dnsResult string
		var finalResult strings.Builder
		var detectedIP string

		for _, ipResult := range successIPs {
			ipType := ipResult[0] // "ipv4" 或 "ipv6"
			ipAddr := ipResult[1] // IP地址
			detectedIP = ipAddr   // 保存检测到的IP

			if ipType == "ipv4" {
				dnsResult = SetDNS("A", ipAddr)
			} else if ipType == "ipv6" {
				dnsResult = SetDNS("AAAA", ipAddr)
			}

			finalResult.WriteString(fmt.Sprintf("类型=%s, IP=%s, 结果=%s\n", ipType, ipAddr, dnsResult))

			if strings.Contains(dnsResult, "IP未改变") || strings.Contains(dnsResult, "成功") {
				result.result = C.CString(fmt.Sprintf("✅ 成功!\n%s", finalResult.String()))
				result.ipAddr = C.CString(detectedIP)
				return result
			}
		}

		result.result = C.CString(fmt.Sprintf("ℹ️ 执行完成:\n%s", finalResult.String()))
		result.ipAddr = C.CString(detectedIP)
		return result
	} else {
		result.result = C.CString("❌ 没有检测到可用的公共IP地址")
		result.ipAddr = C.CString("")
		return result
	}
}

//export FreeDDNSResult
func FreeDDNSResult(result *C.DDNSResult) {
	if result != nil {
		if result.result != nil {
			C.free(unsafe.Pointer(result.result))
		}
		if result.serverIP != nil {
			C.free(unsafe.Pointer(result.serverIP))
		}
		if result.ipAddr != nil {
			C.free(unsafe.Pointer(result.ipAddr))
		}
		C.free(unsafe.Pointer(result))
	}
}

func main() {
	// 保留原有的main函数，用于直接运行Go程序
	result := RunCloudflareDDNS()

	// 打印所有信息
	fmt.Printf("执行结果: %s\n", C.GoString(result.result))
	fmt.Printf("服务器IP: %s\n", C.GoString(result.serverIP))
	fmt.Printf("服务器端口: %d\n", result.serverPort)
	fmt.Printf("超时时间: %d秒\n", result.timeout)
	fmt.Printf("检测到的IP: %s\n", C.GoString(result.ipAddr))

	FreeDDNSResult(result)
}

//go build -buildmode=c-shared -o libcloudflare_ddns.so cloudflare_ddns.go
