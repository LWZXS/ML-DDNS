#!/bin/bash

echo "DDNS 客户端安装程序"
echo "=================="

# 检查必要工具
check_dependency() {
    if ! command -v $1 &> /dev/null; then
        echo "错误: 需要 $1 但未安装"
        exit 1
    fi
}

echo "检查依赖..."
check_dependency gcc
check_dependency go

# 选择编译版本
echo ""
echo "请选择要编译的版本:"
echo "1) C 版本 (推荐)"
echo "2) Go 版本"
echo "3) 两个版本都编译"
read -p "请输入选择 (1/2/3): " choice

case $choice in
    1)
        echo "编译 C 版本..."
        cd Client/build/
        bash build_c.sh
        ;;
    2)
        echo "编译 Go 版本..."
        cd Client/build/
        bash build_go.sh
        ;;

    3)
        echo "编译两个版本..."
        cd Client/build/
        bash build_go.sh
        bash build_c.sh
        ;;
    *)
        echo "无效选择"
        exit 1
        ;;
esac

echo ""
echo "安装完成！"
echo "请按以下步骤操作："
echo "1. 复制示例配置文件: cp Client/examples/config.example.json config.json"
echo "2. 编辑 config.json 文件，填入你的配置信息"
echo "3. 运行客户端: ./ddns-client-go 或 ./ddns-client-c"