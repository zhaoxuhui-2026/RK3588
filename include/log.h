#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdint.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_MODULE  "rk3588"

/* 初始化日志文件 */
int log_init(const char *filename);
int log_init_auto(void);

/* 计算日志文件大小 */
long file_size(FILE *fp);

long long extract_timestamp(const char *name);

/* 对旧日志做压缩处理 */
int log_compress_file(const char *dir, const char *filename);

/* 压缩程序启动前上一个日志 */
void log_find_last_file(void);

/* 判断是否删除旧的日志 */
void log_if_remove_old_file(void);

long log_check_file_if_oversize(void);

/* 关闭日志写入 */
void log_close(void);

/* 设置最低输出等级 */
void log_set_level(int level);

/* 日志接口 */
void log_printf(int level, const char *module,
                 const char *fmt, ...);
                 
/* ASCII日志 */
// void hex_ascii_dump(const char *prefix,
//                     const unsigned char *buf,
//                     int len)

/* 串口打印：
    字符串 
    HEX 
    ASCII */
void serial_hexdump(const char *prefix,
                    uint8_t *buf, int len);

/* 快捷宏 */
#define LOGD(module, ...) \
    log_printf(LOG_LEVEL_DEBUG, __FUNCTION__, __VA_ARGS__)

#define LOGI(module, ...) \
    log_printf(LOG_LEVEL_INFO,  __FUNCTION__, __VA_ARGS__)

#define LOGW(module, ...) \
    log_printf(LOG_LEVEL_WARN,  __FUNCTION__, __VA_ARGS__)

#define LOGE(module, ...) \
    log_printf(LOG_LEVEL_ERROR, __FUNCTION__, __VA_ARGS__)

#endif