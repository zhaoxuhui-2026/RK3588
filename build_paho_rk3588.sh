#!/bin/bash
set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# ================= 工具链 =================
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++

# ================= 路径 =================
PAHO_DIR=paho.mqtt.c
INSTALL_DIR=$(pwd)/paho_install_rk3588

# ================= 检查 =================
command -v $CC >/dev/null || error "交叉编译器 $CC 不存在"
command -v make >/dev/null || error "make 未安装"

[ -d "$PAHO_DIR" ] || error "paho.mqtt.c 目录不存在"

info "✅ 环境检查通过"

# ================= 编译 =================
cd $PAHO_DIR

info "🧹 清理旧编译111111111111111..."
make clean || true

info "🔧 编译 Paho Embedded MQTT C（无 SSL）..."

make \
  CC=$CC \
  CFLAGS="-Os -Wall -fPIC -DNO_OPENSSL" \
  LDFLAGS="" \
  -j$(nproc)

# ================= 安装 =================
info "📦 安装到 $INSTALL_DIR..."

mkdir -p $INSTALL_DIR/include
mkdir -p $INSTALL_DIR/lib

cp src/MQTTClient.h $INSTALL_DIR/include/
cp build/output/libpaho-mqtt3c.a $INSTALL_DIR/lib/ 2>/dev/null || \
cp libpaho-mqtt3c.a $INSTALL_DIR/lib/

info "✅ 编译完成！"
info "📁 头文件: $INSTALL_DIR/include"
info "📁 静态库: $INSTALL_DIR/lib/libpaho-mqtt3c.a"