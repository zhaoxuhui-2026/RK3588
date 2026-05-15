#!/bin/bash

# ================= 交叉编译工具 =================
export CROSS_COMPILE=aarch64-linux-gnu-
CC=${CROSS_COMPILE}gcc
CXX=${CROSS_COMPILE}g++

# ================= 工程路径 =================
SRC_DIR=src
INC_DIR=include

# ================= OpenCV 路径 (在 Ubuntu 本地的模拟目录) =================
OPENCV_INCLUDE=~/code/rk3588/opencv_arm64/include/opencv4
OPENCV_LIB_DIR=~/code/rk3588/opencv_arm64/lib
OPENCV_LIBS="-lopencv_core -lopencv_imgcodecs -lopencv_videoio -lopencv_highgui -lopencv_imgproc"

# Embedded MQTT
MQTT_CLIENT_DIR=mqtt/paho_embedded/client/src
MQTT_PACKET_DIR=mqtt/paho_embedded/packet

TARGET=rk3588

# ================= 编译 =================
echo "开始编译 $TARGET ..."
echo "Current directory: $(pwd)"

$CXX -o $TARGET \
    $SRC_DIR/main.cpp \
    $SRC_DIR/serial_port.c \
    $SRC_DIR/chassis_protocol.c \
    $SRC_DIR/log.c \
    $MQTT_CLIENT_DIR/MQTTClient.c \
    $MQTT_CLIENT_DIR/linux/MQTTLinux.c \
    $MQTT_PACKET_DIR/MQTTPacket.c \
    $MQTT_PACKET_DIR/MQTTSerializePublish.c \
    $MQTT_PACKET_DIR/src/MQTTConnectClient.c \
    $MQTT_PACKET_DIR/src/MQTTSubscribeClient.c \
    $MQTT_PACKET_DIR/src/MQTTUnsubscribeClient.c \
    $MQTT_PACKET_DIR/src/MQTTDeserializePublish.c \
    -I$INC_DIR \
    -I$MQTT_CLIENT_DIR \
    -I$MQTT_PACKET_DIR \
    -I$OPENCV_INCLUDE \
    -L$OPENCV_LIB_DIR \
    $OPENCV_LIBS \
    -lpthread

if [ $? -ne 0 ]; then
    echo "❌ 编译失败"
    exit 1
fi

echo "✅ 编译完成"

# ================= 检查架构 =================
file $TARGET