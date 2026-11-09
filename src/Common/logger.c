#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
static CRITICAL_SECTION g_log_cs;
static bool g_cs_initialized = false;
#else
#include <pthread.h>
#include <sys/time.h>
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static FILE* g_log_file = NULL;
static char g_log_filepath[1024] = {0};
static bool g_initialized = false;

static const char* level_to_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "INFO";
    }
}

static void lock_logger(void) {
#if defined(_WIN32)
    if (g_cs_initialized) EnterCriticalSection(&g_log_cs);
#else
    pthread_mutex_lock(&g_log_mutex);
#endif
}

static void unlock_logger(void) {
#if defined(_WIN32)
    if (g_cs_initialized) LeaveCriticalSection(&g_log_cs);
#else
    pthread_mutex_unlock(&g_log_mutex);
#endif
}

bool logger_init(const char* filepath) {
    lock_logger();
    if (g_initialized && g_log_file) {
        unlock_logger();
        return true;
    }

#if defined(_WIN32)
    if (!g_cs_initialized) {
        InitializeCriticalSection(&g_log_cs);
        g_cs_initialized = true;
    }
#endif

    const char* path = (filepath && filepath[0] != '\0') ? filepath : "sodium.log";
    strncpy(g_log_filepath, path, sizeof(g_log_filepath) - 1);

    g_log_file = fopen(g_log_filepath, "a");
    if (!g_log_file) {
        // Fallback to stderr if file cannot be opened
        fprintf(stderr, "[Sodium Logger] Failed to open log file: %s\n", g_log_filepath);
        unlock_logger();
        return false;
    }

    g_initialized = true;
    unlock_logger();

    LOG_INFO("CORE", "================================================================================");
    LOG_INFO("CORE", "Sodium Automated Reasoning Engine Session Started");
    LOG_INFO("CORE", "Logging output initialized to: %s", g_log_filepath);
    return true;
}

void logger_log(LogLevel level, const char* component, const char* format, ...) {
    if (!g_initialized || !g_log_file) {
        // Auto-initialize with default if not explicitly called yet
        logger_init("sodium.log");
        if (!g_log_file) return;
    }

    // Get current wall-clock timestamp with milliseconds
    char time_buf[64] = {0};
    int millis = 0;

#if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    millis = st.wMilliseconds;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    millis = (int)(tv.tv_usec / 1000);
#endif

    lock_logger();

    if (g_log_file) {
        fprintf(g_log_file, "[%s.%03d] [%-5s] [%-8s] ",
                time_buf,
                millis,
                level_to_string(level),
                component ? component : "GENERAL");

        va_list args;
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);

        fprintf(g_log_file, "\n");
        fflush(g_log_file); // Ensure atomic persistence against crashes
    }

    unlock_logger();
}

const char* logger_get_filepath(void) {
    return g_log_filepath;
}

void logger_flush(void) {
    lock_logger();
    if (g_log_file) {
        fflush(g_log_file);
    }
    unlock_logger();
}

void logger_shutdown(void) {
    lock_logger();
    if (g_initialized && g_log_file) {
        LOG_INFO("CORE", "Sodium Session Terminated Cleanly.");
        LOG_INFO("CORE", "================================================================================");
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
        g_initialized = false;
    }
    unlock_logger();

#if defined(_WIN32)
    if (g_cs_initialized) {
        DeleteCriticalSection(&g_log_cs);
        g_cs_initialized = false;
    }
#endif
}
