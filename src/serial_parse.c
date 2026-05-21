#include "serial_parse.h"
#include "log.h"
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <cstring>

#define CHASSIS_NAV_RESULT          "nav_result"
#define CHASSIS_SYS_BOOT            "sys:boot"
#define CHASSIS_BASE_VEL            "base_vel"
#define CHASSIS_CHECK_SENSOR        "check_sensors"

int parse_serial_cmd(const char *buf)
{
    if (NULL == buf)
    {
        LOGE(LOG_MODULE, "recive serial buf is null!\n");
        return -1;
    }

    if (strstr(buf, CHASSIS_NAV_RESULT) != NULL)
    {
        return CMD_NAV_RESULT;
    }
    if (strstr(buf, CHASSIS_SYS_BOOT) != NULL)
    {
        return CMD_SYS_BOOT;
    }
    if (strstr(buf, CHASSIS_BASE_VEL) != NULL)
    {
        return CMD_BASE_VEL;
    }
    if (strstr(buf, CHASSIS_CHECK_SENSOR) != NULL)
    {
        return CMD_CHECK_SENSOR;
    }

    return CMD_UNKNOWN;
}

bool has_digit_1_to_4(const char *field)
{
    if (!field) return false;

    while (*field) {
        if (*field >= '1' && *field <= '4')
            return true;
        field++;
    }
    return false;
}

const char *find_target_field(const char *str, size_t len)
{
    if (!str || len == 0)
        return NULL;

    /* 找 '{' */
    const char *p = (const char *)memchr(str, '{', len);
    if (!p || p + 1 >= str + len)
        return NULL;

    p++; // skip '{'

    /* 找前两个空格 */
    for (int space = 0; space < 2; space++) {
        size_t remain = str + len - p;
        p = (const char *)memchr(p, ' ', remain);
        if (!p)
            return NULL;
        p++; // 跳过空格
    }

    const char *field = p;

    /* 找第三个空格 */
    size_t remain = str + len - field;
    const char *end = (const char *)memchr(field, ' ', remain);
    if (!end)
        end = str + len;

    static char out[64];
    size_t flen = end - field;

    if (flen >= sizeof(out))
        flen = sizeof(out) - 1;

    memcpy(out, field, flen);
    out[flen] = '\0';

    return out;
}

/*
 * 功能：
 *   从 buf 中解析 "nav_result" 后面的连续字母数字字符串，并解析是否需要拍照
 * 参数：
 *   buf      : 输入字符串（串口/协议数据）
 *   out_ret  : 输出当前是否上报拍照
 *   max_len  : 输出缓冲区最大长度
 * 返回：
 *   成功返回 0，失败返回 -1
 */
int parse_nav_result_value(const char *buf, bool *out_ret)
{
    if (NULL == buf || NULL == out_ret)
    {
        LOGE(LOG_MODULE, "recive buf is null!\n");
        return -1;
    }

    const char *prefix = "nav_result{";
    const char *pos;
    static char serial = '0';
    bool has_digit = false;
    size_t read_ret;
    int max_len = strlen(buf);

    /* 1. 查找前缀 */
    pos = strstr(buf, prefix);
    if (pos == NULL) {
        return -1;
    }

    /* 2. 跳过前缀，指向数据区 */
    pos += strlen(prefix);
    
    int val1, val2, val3;
    char *name = NULL;
    int parsed = sscanf(pos, "%d %d ",
                        &val1, &val2);

    // if (parsed < 2) {
    //     LOGE(LOG_MODULE, "[PARSE] nav_result format error\n");
    //     return -1;
    // }
    read_ret = strlen(buf);
    const char *field = find_target_field(buf, read_ret);
    if (field)
    {
        has_digit = has_digit_1_to_4(field);
        if (has_digit)
        {
            LOGI(LOG_MODULE, "has_digit_1_to_4 return true!\n");
        }
        else
        {
            LOGE(LOG_MODULE, "find target field maybe NULL or cannot find 1_to_4!\n");
        }
    }

    LOGE(LOG_MODULE, "[PARSE] nav_result val1 = %d,  val2 = %d, has_digit = %d\n", val1, val2, has_digit);
    // if ((val1 == 3 || val1 == 1 || val3 == 0)) {
    if (val1 == 3 && true == has_digit) {
        /* 查找字符1～4 */
        // for (int i = 0; buf[i] != '\0'; i++) {
        //     if (buf[i] >= '1' && buf[i] <= '4') {
                // if (serial != buf[i])
                // {
                    *out_ret = true;
                    // serial = buf[i];
                // }
            // }
        // }
    }

    return 0;
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
    if (NULL == buf || NULL == out_str)
    {
        LOGE(LOG_MODULE, "recive buf is null!\n");
        return -1;
    }

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
    if (NULL == buf || NULL == out_value)
    {
        LOGE(LOG_MODULE, "recive buf is null!\n");
        return -1;
    }

    const char *prefix = "base_vel[";
    const char *pos;

    /* 1. 查找前缀 */
    pos = strstr(buf, prefix);
    if (pos == NULL) {
        return -1;
    }

    /* 2. 跳过前缀，指向数据区 */
    pos += strlen(prefix);

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
    if (NULL == buf || NULL == out_value)
    {
        LOGE(LOG_MODULE, "recive buf is null!\n");
        return -1;
    }

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

/* 构造协议帧 */
int build_frame(const char *hostname, uint8_t *frame, int max_len)
{
    if (NULL == hostname || NULL == frame)
    {
        LOGE(LOG_MODULE, "recive buf is null!\n");
        return -1;
    }

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