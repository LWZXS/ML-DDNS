#!/bin/bash


echo "编译服务端..."

# 编译 C 服务端
echo "编译 server.c..."
rm ./bin/server
mkdir bin
gcc -o ./bin/server server.c

echo "编译完成！"
echo "可执行文件: ./bin/server"