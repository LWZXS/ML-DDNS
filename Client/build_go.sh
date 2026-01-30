#!/bin/bash

echo "编译共享库文件..."

# 进入libs目录
cd ./libs/

rm libpublic_address_detector.so
rm libcloudflare_ddns.so
rm libcloudflare_ddns.h

# 编译公网地址检测器 C 模块
echo "编译 libpublic_address_detector.so..."
gcc -fPIC -shared -o libpublic_address_detector.so public_address_detector.c -I. -lpthread


# 编译 Cloudflare DDNS Go 模块
echo "编译 libcloudflare_ddns.so..."
go build -buildmode=c-shared -o libcloudflare_ddns.so cloudflare_ddns.go


echo "共享库编译完成！"

# 进入主目录
cd ../

echo "编译 Go 版本客户端..."

# 编译主程序
echo "编译 main.go..."
mkdir bin
rm ./bin/ddns-client-go main.go
go build -o ./bin/ddns-client-go main.go

echo "Go 版本编译完成！"
echo "可执行文件: ddns-client-go"