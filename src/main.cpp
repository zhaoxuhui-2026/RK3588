#include <stdio.h>
#include <unistd.h>
#include "serial_port.h"
#include "chassis_protocol.h"
#include "log.h"
#include <pthread.h>
#include "MQTTClient.h"
#include "linux/MQTTLinux.h"
#include "cjson/cJSON.h"
#include <ctype.h>
// #include "camera_utils.hpp"

// #define MQTT_HOST   "tcp://mqtt.rmbot.cn:1883"
#define MQTT_BROKER_IP "192.168.0.1"
#define MQTT_BROKER_PORT 1883
#define CLIENT_ID   "rk3588_chassis_001"
#define TOKEN       "token"
#define ROBOT_HOSTNAME "rk3588_chassis_001"

#define CLIENTID    "RobotFaceTriggerPublisher"
#define TOPIC       "robot/face/trigger"
#define QOS         1

/* MQTT */
#define TOPIC_STATUS "robot/rk3588_chassis_001/status"
#define TOPIC_CTRL   "robot/rk3588_chassis_001/control"
#define TOPIC_HEARTBEAT   "reeman/calling/robot/" ROBOT_HOSTNAME "/v2/heartbeat"
#define TOPIC_NAVIGATION "reeman/calling/phone/" ROBOT_HOSTNAME "/v2/points/request/navigationinform"

/* RS232 */
#define RS232_HOSTNAME_REQ  "hostname:get"
#define RS232_UPDATE_DYNAMIC_REQ  "update_dynamic"

#define PUBLISH_MESSAGE  "Hello from RK3588!"
#define KEEP_ALIVE_INTERVAL_MS 5000   // 5 秒

static Network network;
static MQTTClient mqtt_client;

static pthread_t reconnect_tid;
static pthread_t keepalive_tid;
static int keepalive_running = 0;

int sensor_value = 0;
int val_value[2] = {0};
char sys_hostname[64] = {0};

void mqtt_message_arrived(MessageData *md)
{
    LOGI(LOG_MODULE, "[MQTT] ctrl recv: %.*s\n",
           md->message->payloadlen,
           (char *)md->message->payload);

    // TODO: 解析 JSON / 自定义协议
    // chassis_control(cmd);
}

/* 构造协议帧 */
int build_frame(const char *hostname, uint8_t *frame, int max_len)
{
    char data[128];
    int data_len;

    /* 数据段 D */
    data_len = snprintf(data, sizeof(data), "%s", hostname);
    if (data_len <= 0 || data_len >= max_len)
        return -1;

    /* 帧头 H + 长度 L + 数据 D */
    int index = 0;

    frame[index++] = 0xAA;
    frame[index++] = 0x54;
    frame[index++] = (char)data_len;

    memcpy(&frame[index], data, data_len);
    index += data_len;

    /* 校验位 S */
    char checksum = (char)data_len;
    for (int i = 0; i < data_len; i++) {
        checksum ^= data[i];
    }
    frame[index++] = checksum;

    return index;
}

/*
 * 功能：
 *   从 buf 中解析 "sys:boot:" 后面的连续字母数字字符串
 * 参数：
 *   buf      : 输入字符串（串口/协议数据）
 *   out_str  : 输出字符串缓冲区
 *   max_len  : 输出缓冲区最大长度
 * 返回：
 *   成功返回 0，失败返回 -1
 */
int parse_hostname_value(const char *buf, char *out_str)
{
    const char *prefix = "sys:boot:";
    const char *pos;
    int max_len = strlen(buf);

    /* 1. 查找前缀 */
    pos = strstr(buf, prefix);
    if (pos == NULL) {
        return -1;
    }

    /* 2. 跳过前缀，指向数据区 */
    pos += strlen(prefix);

    /* 3. 读取连续的 [0-9 A-Z a-z] */
    int i = 0;
    while (*pos &&
           i < max_len - 1 &&
           (isalnum((unsigned char)*pos))) {
        out_str[i++] = *pos++;
    }

    /* 4. 字符串结束符 */
    out_str[i] = '\0';

    /* 5. 判断是否解析到有效内容 */
    if (i == 0) {
        return -1;
    }

    return 0;
}

/*
 * @brief 从 base_val{...} 中获取传感器值
 * @param buf 串口接收到的字符串
 * @param out_value 解析值
 * @return 0 成功，-1 失败
 */
int parse_base_val_value(const char *buf, int *out_value)
{
    const char *prefix = "base_vel[";
    const char *pos;

    /* 1. 查找前缀 */
    pos = strstr(buf, prefix);
    if (pos == NULL) {
        return -1;
    }

    /* 2. 跳过前缀，指向数据区 */
    pos += strlen(prefix);

    /* 3. 按空格解析第 4 个值 */
    int val1, val2;
    int parsed = sscanf(pos, "%d %d",
                        &val1, &val2);

    if (parsed < 2) {
        LOGI(LOG_MODULE, "[PARSE] base_val format error\n");
        return -1;
    }

    out_value[0] = val1;
    out_value[1] = val2;
    return 0;
}

/*
 * @brief 从 check_sensors{...} 中获取第 4 个传感器值
 * @param buf 串口接收到的字符串
 * @param out_value 解析出的第 4 个值
 * @return 0 成功，-1 失败
 */
int parse_check_sensors_value(const char *buf, int *out_value)
{
    const char *prefix = "check_sensors{";
    const char *pos;

    /* 1. 查找前缀 */
    pos = strstr(buf, prefix);
    if (pos == NULL) {
        return -1;
    }

    /* 2. 跳过前缀，指向数据区 */
    pos += strlen(prefix);

    /* 3. 按空格解析第 4 个值 */
    int val1, val2, val3, val4;
    int parsed = sscanf(pos, "%d %d %d %d",
                        &val1, &val2, &val3, &val4);

    if (parsed < 4) {
        LOGE(LOG_MODULE, "[PARSE] check_sensors format error\n");
        return -1;
    }

    *out_value = val4;
    return 0;
}

/* 串口 keepalive 线程 */
void *serial_keepalive_thread(void *arg)
{
    serial_t *ser_232_2 = (serial_t *)arg;
    uint8_t frame[128];
    uint8_t heartbeat[64];
    memcpy(&heartbeat, "{\"keep_connect\"}", strlen("{\"keep_connect\"}"));
    uint8_t robotdata[64];
    memcpy(&robotdata, "{\"robot_type\"}", strlen("{\"robot_type\"}"));
    int len;
    int ret;

    keepalive_running = 1;

    while (keepalive_running) {
        // build_frame(frame, &len, "keep_connect");

        // 发送
        // ret = serial_write(ser_232_2, heartbeat, strlen("{\"keep_connect\"}"));
        // if (ret > 0)
        // {
        //     LOGI(LOG_MODULE, "heartbeat = %s, ret = %d\n", heartbeat, ret);
        // }
        // else
        // {
        //     LOGE(LOG_MODULE, "send heartbeat error!\n");
        // }
        // memset(frame, 0x00, 64);

        // build_frame(frame, &len, "robot_type");

        // 发送
        // ret = serial_write(ser_232_2, robotdata, strlen("{\"robot_type\"}"));
        // if (ret > 0)
        // {
        //     LOGI(LOG_MODULE, "robotdata = %s, ret = %d\n", robotdata, ret);
        // }
        // else
        // {
        //     LOGE(LOG_MODULE, "send robotdata error!\n");
        // }

        // 获取底盘hostname
        len = build_frame(RS232_HOSTNAME_REQ, frame, sizeof(frame));
        if (len > 0) 
        {
            ret = serial_write(ser_232_2, frame, len);
            if (ret > 0)
            {
                LOGI(LOG_MODULE, "hostname = %s, ret = %d\n", RS232_HOSTNAME_REQ, ret);
            }
            else
            {
                LOGE(LOG_MODULE, "send hostname error!\n");
            }
        }
        // serial_hexdump("[SERIAL TX]", frame, 128);
        memset(frame, 0x00, 128);

        // 获取导航动态参数
        len = build_frame(RS232_UPDATE_DYNAMIC_REQ, frame, sizeof(frame));
        if (len > 0) 
        {
            ret = serial_write(ser_232_2, frame, len);
            if (ret > 0)
            {
                LOGI(LOG_MODULE, "update_dynamic = %s, ret = %d\n", RS232_UPDATE_DYNAMIC_REQ, ret);
            }
            else
            {
                LOGE(LOG_MODULE, "send update_dynamic error!\n");
            }
        }
        // serial_hexdump("[SERIAL TX]", frame, 128);
        memset(frame, 0x00, 128);

        usleep(KEEP_ALIVE_INTERVAL_MS * 1000);
    }

    return NULL;
}

void *mqtt_reconnect_thread(void *arg)
{
    (void)arg;

    while (1) {
        if (!MQTTIsConnected(&mqtt_client)) {
            LOGE(LOG_MODULE, "[MQTT] disconnected, try reconnect...\n");

            MQTTPacket_connectData data =
                MQTTPacket_connectData_initializer;
            data.clientID.cstring = CLIENT_ID;
            data.username.cstring = TOKEN;
            data.password.cstring = TOKEN;

            int rc = MQTTConnect(&mqtt_client, &data);
            if (rc == 0) {
                LOGI(LOG_MODULE, "[MQTT] Reconnected successfully!\n");
                // 重新订阅
                MQTTSubscribe(&mqtt_client, TOPIC_CTRL, QOS1, mqtt_message_arrived);
                MQTTSubscribe(&mqtt_client, TOPIC_HEARTBEAT, QOS1, mqtt_message_arrived);
                MQTTSubscribe(&mqtt_client, TOPIC_NAVIGATION, QOS1, mqtt_message_arrived);
            } else {
                LOGE(LOG_MODULE, "[MQTT] Reconnect failed, rc=%d, retry in 5s...\n", rc);
            }
        }
        sleep(5);
    }
}

void *mqtt_publish_thread(void *arg)
{
    (void)arg;
    MQTTMessage message;
    char payload[256];
    char camera_status[32];
    char robot_status[32];
    char robot_hostname[64];
    long long timestamp = 0;

    while (1) {
        if (MQTTIsConnected(&mqtt_client)) {

            timestamp = (long long)time(NULL) * 1000; // 当前时间戳（毫秒

            if (sensor_value == 1)
            {
                sprintf(camera_status, "normal", strlen("normal"));
            }
            else
            {
                // sprintf(camera_status, "error", strlen("error"));

                /* Demo */
                sprintf(camera_status, "normal", strlen("normal"));
            }

            if (strlen(sys_hostname) > 0)
            {
                sprintf(robot_hostname, sys_hostname, strlen(sys_hostname));
            }
            else
            {
                sprintf(robot_hostname, "fbot31b-250307-007-001", strlen("fbot31b-250307-007-001"));
            }
        
            // if (val_value[0] != 0 && val_value[1] != 0)
            // {
            //     sprintf(robot_status, "normal", strlen("running"));
            // }
            // else
            // {
            //     sprintf(robot_status, "idle", strlen("idle"));
            // }
            LOGI(LOG_MODULE, "sensor_value = %d, camera_status = %s, val_value[0] = %d, val_value[1] = %d, robot_status = %d\n",
             sensor_value, camera_status, val_value[0], val_value[1], robot_status);

            snprintf(payload, sizeof(payload),
                "{"
                "\"robot_id\":\"%s\","
                "\"robot_type\":2,"
                "\"robot_status\":\"normal\","
                "\"camera_status\":\"%s\","
                "\"start_status\":\"activated\","
                "\"timestamp\":%lld"
                "}",
                robot_hostname,
                camera_status,
                timestamp
            );
            memset(&message, 0, sizeof(message));
            message.qos = QOS0;
            message.retained = 0;
            message.dup = 0;
            message.payload = (void *)payload;
            message.payloadlen = strlen(payload);

            LOGI(LOG_MODULE, "[MQTT] try publish message\n");
            MQTTPublish(&mqtt_client, TOPIC, &message);
        }
        sleep(5);
    }
}

int main() {
    uint8_t chassis_rx_buf[1024], rk3128_rx_buf[1024];
    serial_t chassis_serial, rk3128_serial;
    chassis_state_t state;
    int ret = 0;
    
    log_init_auto(); 

    if (serial_open(&chassis_serial, "/dev/ttyS8", 115200) < 0) {
        LOGI(LOG_MODULE, "Serial opened fail !!\n");
        return -1;
    }

    if (serial_open(&rk3128_serial, "/dev/ttyS9", 115200) < 0) {
        LOGI(LOG_MODULE, "Serial opened fail !!\n");
        return -1;
    }

    LOGI(LOG_MODULE, "Serial opened, start receiving...\n");

    /* MQTT */
    NetworkInit(&network);
    ret = NetworkConnect(&network, MQTT_BROKER_IP, MQTT_BROKER_PORT);
    if (ret != 0) {
        LOGI(LOG_MODULE, "[MQTT] Network connect failed, ret=%d\n", ret);
        return -1;
    } else {
        LOGI(LOG_MODULE, "[MQTT] Network connect successfully!\n");
    }

    MQTTClientInit(&mqtt_client, &network, 1000, (unsigned char *)malloc(1000), 1000, (unsigned char *)malloc(1000), 1000);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.clientID.cstring = CLIENT_ID;
    data.username.cstring = TOKEN;
    data.password.cstring = TOKEN;

    ret = MQTTConnect(&mqtt_client, &data);
    if (ret != 0)
    {
        LOGI("main", "[MQTT] Connected successfully!\n");
    }
    else
    {
        LOGE("main", "[MQTT] Connect fail! ret = %d\n", ret);
    }

    /* 订阅控制 */
    MQTTSubscribe(&mqtt_client, TOPIC_CTRL, QOS1, mqtt_message_arrived);
    MQTTSubscribe(&mqtt_client, TOPIC_HEARTBEAT, QOS1, mqtt_message_arrived);
    MQTTSubscribe(&mqtt_client, TOPIC_NAVIGATION, QOS1, mqtt_message_arrived);

    /* 重连线程 */
    pthread_create(&reconnect_tid, NULL, mqtt_reconnect_thread, NULL);
    
    /* 串口心跳线程 */
    pthread_create(&keepalive_tid, NULL, serial_keepalive_thread, &chassis_serial);

    /* 模拟线程 */
    pthread_t navi_tid;
    pthread_create(&navi_tid, NULL, mqtt_publish_thread, NULL);

    // // 使用默认摄像头
    // Camera cam(0);

    // if (!cam.is_open()) {
    //     return -1;
    // }

    // cam.capture("capture.jpg");

    while (1) {

        MQTTYield(&mqtt_client, 1000);
        if (!MQTTIsConnected(&mqtt_client)) {
            LOGI(LOG_MODULE, "[MQTT] Connection lost after yield!\n");
        }

        int read_ret = serial_read(&chassis_serial, chassis_rx_buf, sizeof(chassis_rx_buf));
        if (read_ret > 0) {
            for (int i = 0; i < read_ret; i++) {
                if (chassis_parse(chassis_rx_buf[i], &state)) {
                    char payload[128];
                    snprintf(payload, sizeof(payload),
                             "{\"vel\":%d,\"ang\":%d}",
                             state.velocity,
                             state.angular);

                    // MQTTPublish(&mqtt_client, TOPIC_STATUS, payload);
                    // printf("Vel=%d Ang=%d\n",
                    //        state.velocity, state.angular);
                }
            }
            
            // if (parse_check_sensors_value((char *)chassis_rx_buf, &sensor_value) == 0) {
            //     LOGI("main", "[SERIAL] check_sensors 4th value = %d\n", sensor_value);
            // }
            // if (parse_base_val_value((char *)chassis_rx_buf, val_value) == 0) {
            //     LOGI("main", "[SERIAL] base_val line_speed = %d, angular_speed = %d\n", val_value[0], val_value[1]);
            // }
            // if (parse_hostname_value((char *)chassis_rx_buf, sys_hostname)) {
            //     LOGI("main", "[SERIAL] hostname = %d\n", sys_hostname);
            // }

            // printf("chassis_rx_buf = %s\n", chassis_rx_buf + 2);
            // serial_hexdump("[SERIAL RX]", chassis_rx_buf, n);

            /* Write Data to RK3128 */
            ret = serial_write(&rk3128_serial, chassis_rx_buf, read_ret);
            if (ret > 0)
            {
                LOGI(LOG_MODULE, "serial_write = %s, ret = %d\n", chassis_rx_buf, ret);
            }
            else
            {
                LOGE(LOG_MODULE, "send hostname error!\n");
            }
        }

        /* RS232_3 */
        read_ret = serial_read(&rk3128_serial, rk3128_rx_buf, sizeof(rk3128_rx_buf));
        if (read_ret > 0) {
            for (int i = 0; i < read_ret; i++) {
                if (chassis_parse(rk3128_rx_buf[i], &state)) {
                    char payload[128];
                    snprintf(payload, sizeof(payload),
                             "{\"vel\":%d,\"ang\":%d}",
                             state.velocity,
                             state.angular);
                }
            }

            serial_hexdump("[RS232_3 To SERIAL RX]", rk3128_rx_buf, read_ret);

            /* Write Data to RS232_2 */
            ret = serial_write(&chassis_serial, rk3128_rx_buf, read_ret);
            if (ret > 0)
            {
                LOGI(LOG_MODULE, "serial_write = %s, ret = %d\n", rk3128_rx_buf, ret);
            }
            else
            {
                LOGE(LOG_MODULE, "send hostname error!\n");
            }
        }

        usleep(10000);
    }

    serial_close(&chassis_serial);
    serial_close(&rk3128_serial);
    return 0;
}
