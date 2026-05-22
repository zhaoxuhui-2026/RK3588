#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#define LOG_DIR           "/user_space/user/logs"
#define LOG_PREFIX        "rk3588_"
#define LOG_MAX_SIZE      (50 * 1024 * 1024)  // 50MB
#define LOG_MAX_FILES     168                 // 最多存储168份日志文件，满足存储设备一个星期以上日志量

static FILE *log_fp = NULL;
static int   log_level = LOG_LEVEL_INFO;
static int log_file_num = 0;
// static long log_cur_size = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
char log_file_name[128] = {0};

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
    // printf("%s [%s] [%s] ",
    //        time_buf, level_str[level], module);
    // va_start(args, fmt);
    // vprintf(fmt, args);
    // printf("\n");
    // va_end(args);

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
    
    log_check_file_if_oversize();

    pthread_mutex_unlock(&log_mutex);
}

int make_log_filename(char *buf, size_t size)
{
    struct tm tm_info;
    time_t now = time(NULL);

    localtime_r(&now, &tm_info);
    strftime(buf, size,
             LOG_DIR "/rk3588_%Y%m%d_%H%M%S.log",
             &tm_info);
    return 0;
}

int log_init_auto(void)
{
    struct stat st;
    if (stat(LOG_DIR, &st) != 0) {
        mkdir(LOG_DIR, 0755);
    }
    // char name[256] = {0};
    memset(log_file_name, 0, 128);
    make_log_filename(log_file_name, sizeof(log_file_name));

    log_fp = fopen(log_file_name, "a");
    if (!log_fp) {
        perror("log_init_auto fopen failed");
        printf("log_init_auto fopen failed");
        return -1;
    }

    // log_cur_size = 0;
    return 0;
}

long file_size(FILE *fp)
{
    if (NULL == fp)
    {
        LOGE(LOG_MODULE, "file size error fp NULL!");
        return 0;
    }

    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, cur, SEEK_SET);  // 回到原位置
    return size;
}

void log_close(void)
{
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
    // log_cur_size = 0;
}

long long extract_timestamp(const char *name)
{
    const char *p = strstr(name, "rk3588_");
    if (!p) return 0;

    p += strlen("rk3588_");

    char buf[16] = {0};
    strncpy(buf, p, 15);  // YYYYMMDD_HHMMSS

    /* 去掉下划线 */
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '_') 
        {
            // buf[i] = '\0';
            for (int j = i; buf[j]; j++) {
                buf[j] = buf[j+1];
            }
        }
    }

    return atoll(buf);  // 20260115103812
}

int log_compress_file(const char *dir, const char *filename)
{
    char cmd[128];

    /* 构造命令：gzip -f 文件名 */
    snprintf(cmd, sizeof(cmd),
             "cd '%s' && gzip -f '%s'",
             dir, filename);

    int ret = system(cmd);
    if (ret == 0) {
        printf("Compressed: %s.gz\n", filename);
        return 0;
    } else {
        perror("gzip compress failed");
        return -1;
    }
}

void log_if_remove_old_file(void)
{
    DIR *dir = opendir(LOG_DIR);
    if (!dir) return;

    struct dirent *entry;
    char oldest_name[256] = {0};
    int file_num = 0;
    long long oldest_ts = LLONG_MAX;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, LOG_PREFIX, strlen(LOG_PREFIX)))
            continue;

        long long ts = extract_timestamp(entry->d_name);
        if (ts == 0) continue;

        if (ts < oldest_ts) {
            oldest_ts = ts;
            strcpy(oldest_name, entry->d_name);
        }
        file_num++;
        // printf("entry->d_name = %s, oldest_name = %s, ts = %ld\n", entry->d_name, oldest_name, ts);
    }
    closedir(dir);

    if (oldest_name[0] && file_num > LOG_MAX_FILES) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", LOG_DIR, oldest_name);
        // printf("path = %s\n", path);
        if (unlink(path) != 0) {
            // printf("Delete failed: %s (errno=%d)\n", strerror(errno), errno);
        }
    }
}

long log_check_file_if_oversize(void)
{
    long log_size = file_size(log_fp);

    if(log_size > LOG_MAX_SIZE)
    {
        log_close();

        // DIR *dir = opendir(LOG_DIR);
        // if (!dir) return 0;
        // struct dirent *entry;
        // int file_num = 0;

        // while ((entry = readdir(dir)) != NULL) {
        //     if (strncmp(entry->d_name, LOG_PREFIX, strlen(LOG_PREFIX))) {
        //         continue;
        //     }
        //     file_num++;
        // }

        // while (1) {
        //     if (file_num > LOG_MAX_FILES)
        //     {
                // log_if_remove_old_file();
        //         file_num--;
        //         log_file_num = file_num;
        //     }
        //     else
        //     {
        //         break;
        //     }
        // }
        log_compress_file(LOG_DIR, log_file_name);
        log_if_remove_old_file();
        log_init_auto();
    }

    return log_size;
}

void serial_hexdump(const char *prefix, uint8_t *buf, int len)
{
    if (!buf || len <= 0)
        return;

    char time_buf[32];
    log_get_time(time_buf, sizeof(time_buf));

    pthread_mutex_lock(&log_mutex);

    /* ===== 终端输出 ===== */
    // printf("%s [SERIAL] %s Len=%d\n",
    //        time_buf, prefix, len);

    // /* HEX */
    // printf("HEX:  ");
    // for (int i = 0; i < len; i++) {
    //     printf("%02X ", buf[i]);
    // }
    // printf("\n");

    // /* ASCII */
    // printf("ASCII:");
    // for (int i = 0; i < len; i++) {
    //     if (buf[i] >= 32 && buf[i] <= 126)
    //         printf(" %c ", buf[i]);
    //     else
    //         printf(" . ");
    // }
    // printf("\n");

    // /* 协议字段 */
    // if (len >= 3) {
    //     printf("Frame: Header=0x%02X%02X, Length=%d, Data=",
    //            buf[0], buf[1], buf[2]);

    //     for (int i = 3; i < len - 1; i++) {
    //         printf("%c", buf[i]);
    //     }
    //     printf(", Checksum=0x%02X\n", buf[len - 1]);
    // }

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
                fprintf(log_fp, "%c", buf[i]);
            else
                fprintf(log_fp, ".");
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
        log_check_file_if_oversize();
    }

    pthread_mutex_unlock(&log_mutex);
}