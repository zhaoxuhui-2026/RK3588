#include <stdio.h>
#include <unistd.h>
#include "serial_port.h"
#include "serial_parse.h"
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
#define CHASSIS_NAV_RESULT_RSP  "nav_result"

#define PUBLISH_MESSAGE  "Hello from RK3588!"
#define KEEP_ALIVE_INTERVAL_MS 5000   // 5 秒

static Network network;
static MQTTClient mqtt_client;

static int keepalive_running = 0;

serial_t chassis_serial, rk3128_serial;

int sensor_value = 0;
int val_value[2] = {0};
char sys_hostname[64] = {0};
bool is_nav_result = false;

void mqtt_message_arrived(MessageData *md)
{
    if (NULL == md)
    {
        LOGE(LOG_MODULE, "mqtt message recive NULL!\n");
        return;
    }

    LOGI(LOG_MODULE, "[MQTT] ctrl recv: %.*s\n",
           md->message->payloadlen,
           (char *)md->message->payload);

    // TODO: 解析 JSON / 自定义协议
    // chassis_control(cmd);
}

int parse_serial_frame(const char *buf)
{
    if (buf == NULL)
    {
        LOGE(LOG_MODULE, "parse serial recive null data!\n");
        return -1;
    }

    char serial_buf[1024] = {0};
    int cmd_type = 0;
    memcpy(serial_buf, buf, 1024);

    cmd_type = parse_serial_cmd(serial_buf);
    LOGI(LOG_MODULE, "[SERIAL] cmd_type = %d\n", cmd_type);
    switch (cmd_type)
    {
        case CMD_NAV_RESULT:
            if (parse_nav_result_value(serial_buf, &is_nav_result)) {
                LOGI("main", "[SERIAL] nav_result = %d\n", is_nav_result);
            }
            break;
        case CMD_SYS_BOOT:
            if (parse_hostname_value(serial_buf, sys_hostname)) {
                LOGI("main", "[SERIAL] hostname = %d\n", sys_hostname);
            }
            break;
        case CMD_BASE_VEL:
            if (parse_base_val_value(serial_buf, val_value) == 0) {
                LOGI("main", "[SERIAL] base_val line_speed = %d, angular_speed = %d\n", val_value[0], val_value[1]);
            }
            break;
        case CMD_CHECK_SENSOR:
            if (parse_check_sensors_value(serial_buf, &sensor_value) == 0) {
                LOGI("main", "[SERIAL] check_sensors 4th value = %d\n", sensor_value);
            }
            break;
    }
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
        // len = build_frame(RS232_UPDATE_DYNAMIC_REQ, frame, sizeof(frame));
        // if (len > 0) 
        // {
        //     ret = serial_write(ser_232_2, frame, len);
        //     if (ret > 0)
        //     {
        //         LOGI(LOG_MODULE, "update_dynamic = %s, ret = %d\n", RS232_UPDATE_DYNAMIC_REQ, ret);
        //     }
        //     else
        //     {
        //         LOGE(LOG_MODULE, "send update_dynamic error!\n");
        //     }
        // }
        // // serial_hexdump("[SERIAL TX]", frame, 128);
        // memset(frame, 0x00, 128);

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

void *nav_recv_thread(void *arg)
{
    char payload[1024];
    uint8_t rk3128_rx_buf[1024] = {0};
    struct timeval tv;
    int read_ret, i, ret = 0;
    int maxfd;
    fd_set rfds;

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(rk3128_serial.fd, &rfds);

        int maxfd = rk3128_serial.fd;

        tv.tv_sec = 1;   // 1 秒超时
        tv.tv_usec = 0;
        
        ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            LOGE(LOG_MODULE, "select error\n");
            usleep(100000);
            continue;
        }

        if (FD_ISSET(rk3128_serial.fd, &rfds)) {
            /* RS232_3 */
            memset(rk3128_rx_buf, 0x00, 1024);
            read_ret = serial_read(&rk3128_serial, rk3128_rx_buf, sizeof(rk3128_rx_buf));
            if (read_ret > 0) {
                // for (i = 0; i < read_ret; i++) {
                //     if (chassis_parse(rk3128_rx_buf[i], &state)) {
                //         snprintf(payload, sizeof(payload),
                //                  "{\"vel\":%d,\"ang\":%d}",
                //                  state.velocity,
                //                  state.angular);
                //     }
                // }
            
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
        }
    }
}

void *chassic_recv_thread(void *arg)
{
    uint8_t chassis_rx_buf[1024] = {0};
    struct timeval tv;
    int i, ret, read_ret = 0;
    int maxfd;
    fd_set rfds;

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(chassis_serial.fd, &rfds);

        int maxfd = chassis_serial.fd;

        tv.tv_sec = 1;   // 1 秒超时
        tv.tv_usec = 0;
        
        ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            LOGE(LOG_MODULE, "select error\n");
            usleep(100000);
            continue;
        }

        if (FD_ISSET(chassis_serial.fd, &rfds)) {
            memset(chassis_rx_buf, 0x00, sizeof(chassis_rx_buf));
            read_ret = serial_read(&chassis_serial, chassis_rx_buf, sizeof(chassis_rx_buf));
            if (read_ret > 0) {
                // for (i = 0; i < read_ret; i++) {
                //     if (chassis_parse(chassis_rx_buf[i], &state)) {
                //         snprintf(payload, sizeof(payload),
                //                  "{\"vel\":%d,\"ang\":%d}",
                //                  state.velocity,
                //                  state.angular);

                //         // MQTTPublish(&mqtt_client, TOPIC_STATUS, payload);
                //         // printf("Vel=%d Ang=%d\n",
                //         //        state.velocity, state.angular);
                //     }
                // }

                // printf("chassis_rx_buf = %s\n", chassis_rx_buf + 2);
                serial_hexdump("[SERIAL RX]", chassis_rx_buf, read_ret);

                if (parse_serial_frame((char *)chassis_rx_buf) == 0)
                {
                    LOGI("main", "[SERIAL] prase serial value = %d\n", sensor_value);
                }
                else
                {
                    LOGE(LOG_MODULE, "parse data error!\n");
                }

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
        }
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
    long long last_timestamp = 0;

    while (1) {
        if (MQTTIsConnected(&mqtt_client) && is_nav_result == true) {

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
            LOGI(LOG_MODULE, "[MQTT] Send sensor_value = %d, camera_status = %s, val_value[0] = %d, val_value[1] = %d, robot_status = %d\n",
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
            if (timestamp - last_timestamp > 1000 * 5) {
                MQTTPublish(&mqtt_client, TOPIC, &message);
            }
            last_timestamp = timestamp;
            is_nav_result = false;
        }
        usleep(5000);
    }
}

int main() {
    uint8_t chassis_rx_buf[1024], rk3128_rx_buf[1024];
    chassis_state_t state;
    char payload[128];
    int ret, read_ret, i = 0;
    
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
    pthread_t reconnect_tid;
    pthread_create(&reconnect_tid, NULL, mqtt_reconnect_thread, NULL);
    
    /* 串口心跳线程 */
    pthread_t keepalive_tid;
    pthread_create(&keepalive_tid, NULL, serial_keepalive_thread, &chassis_serial);

    /* 模拟线程 */
    pthread_t navi_tid;
    pthread_create(&navi_tid, NULL, mqtt_publish_thread, NULL);

    /* 导航模块交互 */
    pthread_t nav_recv_tid;
    pthread_create(&nav_recv_tid, NULL, nav_recv_thread, NULL);

    /* 底盘交互 */
    pthread_t chassis_tid;
    pthread_create(&chassis_tid, NULL, chassic_recv_thread, NULL);

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

        usleep(10000);
    }

    serial_close(&chassis_serial);
    serial_close(&rk3128_serial);
    return 0;
}
