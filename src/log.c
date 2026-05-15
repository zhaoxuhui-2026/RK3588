#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static FILE *log_fp = NULL;
static int   log_level = LOG_LEVEL_INFO;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str[] = {
    "DEBUG", "INFO", "WARN", "ERROR"};

static int mkdir_p(const char *path)
{
    char tmp[256];
    char *p = NULL;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

int log_init(const char *filename)
{
    char dir[256] = {0};
    const char *last_slash = strrchr(filename, '/');

    /* 如果路径中包含目录，先创建目录 */
    if (last_slash) {
        strncpy(dir, filename, last_slash - filename);
        dir[last_slash - filename] = '\0';

        if (mkdir_p(dir) != 0) {
            perror("log_init mkdir_p failed");
            return -1;
        }
    }

    log_fp = fopen(filename, "a");
    if (!log_fp) {
        perror("log_init fopen failed");
        return -1;
    }

    return 0;
}

void log_set_level(int level)
{
    log_level = level;
}

static void log_get_time(char *buf, int len)
{
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);

    snprintf(buf, len, "%02d:%02d:%02d.%03ld",
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             ts.tv_nsec / 1000000);
}

void log_printf(int level, const char *module,
                 const char *fmt, ...)
{
    if (level < log_level)
        return;

    char time_buf[32];
    log_get_time(time_buf, sizeof(time_buf));

    pthread_mutex_lock(&log_mutex);

    va_list args;

    /* 终端输出 */
    printf("%s [%s] [%s] ",
           time_buf, level_str[level], module);
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);

    /* 文件输出 */
    if (log_fp) {
        fprintf(log_fp, "%s [%s] [%s] ",
                time_buf, level_str[level], module);
        va_start(args, fmt);
        vfprintf(log_fp, fmt, args);
        fprintf(log_fp, "\n");
        fflush(log_fp);
        va_end(args);
    }

    pthread_mutex_unlock(&log_mutex);
}


/*
 * 功能：以 HEX + ASCII 方式打印串口帧
 * 参数：
 *   prefix : 打印前缀（如 "[SERIAL TX]"）
 *   buf    : 帧数据
 *   len    : 帧长度
 */
// void hex_ascii_dump(const char *prefix,
//                     const unsigned char *buf,
//                     int len)
// {
//     printf("%s Len=%d\n", prefix, len);

//     /* HEX */
//     printf("HEX: ");
//     for (int i = 0; i < len; i++) {
//         printf("%02X ", buf[i]);
//     }
//     printf("\n");

//     /* ASCII */
//     printf("ASCII: ");
//     for (int i = 0; i < len; i++) {
//         if (isprint(buf[i])) {
//             printf("%c ", buf[i]);
//         } else {
//             printf(". ");
//         }
//     }
//     printf("\n");
// }

int log_init_auto(void)
{
    char filename[128];
    char time_buf[32];
    struct tm tm_info;
    time_t now = time(NULL);

    localtime_r(&now, &tm_info);
    strftime(time_buf, sizeof(time_buf),
             "%Y%m%d_%H%M%S", &tm_info);

    snprintf(filename, sizeof(filename),
             "./rk3588_%s.log", time_buf);

    log_fp = fopen(filename, "a");
    if (!log_fp) {
        perror("log_init_auto fopen failed");
        return -1;
    }

    return 0;
}

// void serial_hexdump(const char *prefix, uint8_t *buf, int len)
// {
//     printf("%s Len=%d\n", prefix, len);

//     char time_buf[32];
//     log_get_time(time_buf, sizeof(time_buf));

//     // 打印 HEX 值
//     printf("HEX:  ");
//     for (int i = 0; i < len; i++) {
//         printf("%02X ", buf[i]);
//     }
//     printf("\n");

//     // 打印 ASCII 值
//     printf("ASCII:");
//     for (int i = 0; i < len; i++) {
//         if (buf[i] >= 32 && buf[i] <= 126) {
//             printf("%c", buf[i]);
//         } else {
//             printf(" . ");
//         }
//     }
//     printf("\n");

//     // 按协议字段拆分打印
//     if (len >= 2) {
//         printf("Frame: Header=0x%02X%02X, Length=%d, Data=", buf[0], buf[1], buf[2]);
//         for (int i = 3; i < len - 1; i++) {
//             printf("%c", buf[i]);
//         }
//         printf(", Checksum=0x%02X\n", buf[len - 1]);
//     }
// }

void serial_hexdump(const char *prefix, uint8_t *buf, int len)
{
    if (!buf || len <= 0)
        return;

    char time_buf[32];
    log_get_time(time_buf, sizeof(time_buf));

    pthread_mutex_lock(&log_mutex);

    /* ===== 终端输出 ===== */
    printf("%s [SERIAL] %s Len=%d\n",
           time_buf, prefix, len);

    /* HEX */
    printf("HEX:  ");
    for (int i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");

    /* ASCII */
    printf("ASCII:");
    for (int i = 0; i < len; i++) {
        if (buf[i] >= 32 && buf[i] <= 126)
            printf(" %c ", buf[i]);
        else
            printf(" . ");
    }
    printf("\n");

    /* 协议字段 */
    if (len >= 3) {
        printf("Frame: Header=0x%02X%02X, Length=%d, Data=",
               buf[0], buf[1], buf[2]);

        for (int i = 3; i < len - 1; i++) {
            printf("%c", buf[i]);
        }
        printf(", Checksum=0x%02X\n", buf[len - 1]);
    }

    /* ===== 文件输出 ===== */
    if (log_fp) {
        fprintf(log_fp, "%s [SERIAL] %s Len=%d\n",
                time_buf, prefix, len);

        fprintf(log_fp, "HEX:  ");
        for (int i = 0; i < len; i++) {
            fprintf(log_fp, "%02X ", buf[i]);
        }
        fprintf(log_fp, "\n");

        fprintf(log_fp, "ASCII:");
        for (int i = 0; i < len; i++) {
            if (buf[i] >= 32 && buf[i] <= 126)
                fprintf(log_fp, " %c ", buf[i]);
            else
                fprintf(log_fp, " . ");
        }
        fprintf(log_fp, "\n");

        if (len >= 3) {
            fprintf(log_fp, "Frame: Header=0x%02X%02X, Length=%d, Data=",
                    buf[0], buf[1], buf[2]);

            for (int i = 3; i < len - 1; i++) {
                fprintf(log_fp, "%c", buf[i]);
            }
            fprintf(log_fp, ", Checksum=0x%02X\n", buf[len - 1]);
        }

        fflush(log_fp);
    }

    pthread_mutex_unlock(&log_mutex);
}