# 动态域名解析（DDNS）客户端

## 项目简介

这是一个跨平台的动态域名解析（DDNS）客户端，支持 Windows、Linux 和 macOS 系统。客户端能够自动检测系统的所有 IP 地址（包括 IPv4
和 IPv6），智能识别公网 IP，并自动更新 Cloudflare DNS 记录。

## 工作原理

1. **IP 地址收集**：客户端自动获取操作系统所有网络接口的 IP 地址（IPv4 和 IPv6）
2. **公网 IP 检测**：
    - 客户端在本机监听随机端口
    - 将每个 IP 地址和监听端口发送给服务端
    - 服务端尝试连接该 IP 和端口
    - 连接成功则判定为公网 IP
3. **DNS 更新**：检测到的公网 IP 将自动更新到 Cloudflare DNS

## 优势特点

- **资源高效**：采用 C 语言和 Go 语言混合开发，主进程在设置成功后退出，仅监控服务常驻
- **智能监控**：后台服务持续监控 IP 变化，变化时自动重新激活主进程更新 DNS
- **跨平台支持**：完整支持 Windows、Linux 和 macOS 系统
- **分离架构**：
    - IP 获取和 DNS 设置使用 Go 语言开发（非常驻）
    - IP 实时检测使用 C 语言开发（常驻服务）

## 快速开始

### 客户端运行

```bash
# 直接运行已编译的客户端
./main
```

![img.png](img.png)

### 配置文件说明

修改 conf/config.json` 文件：

```json
{
  "apiKey": "your_cloudflare_api_key",
  "email": "your_cloudflare_account_email",
  "zoneID": "your_cloudflare_zone_id",
  "domain": "example.com",
  "recordName": "www",
  "serverIP": "ddns_server_ip",
  "serverPort": 8066,
  "timeout": 10
}
```

**配置参数说明**：

- `apiKey`: Cloudflare API 密钥
- `email`: Cloudflare 账户邮箱
- `zoneID`: Cloudflare 区域 ID
- `domain`: 主域名（如：example.com）
- `recordName`: 子域名（如：www）
- `serverIP`: DDNS 服务端 IP 地址
- `serverPort`: 服务端监听端口（默认：8066）
- `timeout`: 超时时间（秒）

## 详细配置指南

### Cloudflare 配置

#### 1. 获取 API 密钥

1. 登录 Cloudflare 控制台
2. 点击右上角用户图标，选择 "My Profile"
3. 进入 "API Tokens" 选项卡
4. 点击 "Create Token"
5. 使用 "Edit zone DNS" 模板
6. 选择需要授权的域名
7. 复制生成的 API 密钥

#### 2. 获取 Zone ID

1. 在 Cloudflare 控制台选择你的域名
2. 在右侧 "API" 部分找到 "Zone ID"
3. 复制该 Zone ID

#### 3. DNS 记录设置

确保在 Cloudflare DNS 中已添加需要更新的记录：

- 类型：A（IPv4）或 AAAA（IPv6）
- 名称：子域名（如 www）
- 内容：任意 IP（客户端会自动更新）
- 代理状态：根据需求选择（橙色云朵为开启代理）


## 自动编译指南
下载
```bash
git clone https://github.com/LWZXS/ML-DDNS.git
```

![img_1.png](img_1.png)

客户端 编译运行
```bash
# 编译为 ./bin/ddns-client-go 或 ./bin/ddns-client-c
cd Client
bash install.sh

# 运行
./bin/ddns-client-go 或 ./bin/ddns-client-c
```

服务端 编译运行
```bash
# 编译为 ./bin/server
cd Server
bash build_c.sh

# 运行
./bin/server
```

## 手动编译指南

### 客户端库文件编译

#### 1. 公网地址检测器（C）

```bash
# 编译为共享库
gcc -fPIC -shared -o libpublic_address_detector.so public_address_detector.c -I. -lpthread
```

#### 2. Cloudflare DDNS 模块（Go）

```bash
# 编译为 C 共享库
go build -buildmode=c-shared -o libcloudflare_ddns.so cloudflare_ddns.go
```

### 客户端入口编译

#### Go 版本入口

```bash
# 编译主程序
go build main.go

# 运行（设置库路径）
DYLD_LIBRARY_PATH=./libs ./main
```

#### C 语言版本入口

```bash
# 编译主程序
gcc -o main main.c -I./libs -L./libs -lpublic_address_detector -lcloudflare_ddns

# 设置环境变量并运行
export DYLD_LIBRARY_PATH=./libs:.
./main
```

## 服务端配置

### 服务端编译与运行

```c
// 修改 server.c 中的监听端口（默认：8066）
#define DEFAULT_PORT 8066
```

**编译命令**：

```bash
gcc -o server server.c
```

**运行服务端**：

```bash
./server
```

## 使用说明

### 运行流程

1. 配置好客户端和服务端
2. 启动服务端程序
3. 运行客户端程序
4. 客户端自动检测 IP 并更新 DNS
5. 监控服务持续运行，检测 IP 变化

### 环境变量

- `DYLD_LIBRARY_PATH`: 指定共享库路径（macOS）
- `LD_LIBRARY_PATH`: 指定共享库路径（Linux）

### 注意事项

1. 确保服务端端口（默认 8066）在防火墙中开放
2. Cloudflare API 密钥需要适当的 DNS 编辑权限
3. 客户端和服务端需要保持网络连通
4. 首次运行可能需要等待 DNS 记录传播

## 故障排除

### 常见问题

1. **客户端无法连接服务端**
    - 检查服务端 IP 和端口配置
    - 确认防火墙设置
    - 验证网络连通性

2. **DNS 更新失败**
    - 检查 Cloudflare API 密钥权限
    - 验证 Zone ID 和域名配置
    - 确认 DNS 记录已存在

3. **公网 IP 检测失败**
    - 确保服务端能够访问客户端
    - 检查客户端网络配置
    - 验证端口监听状态

### 日志检查

客户端运行时会输出检测和更新日志，可根据日志信息进行问题诊断。

---

**提示**：本工具适用于动态 IP 环境下的域名解析，特别适合家庭宽带、移动网络等 IP 经常变化的场景。