#ifndef SODIUM_LOGGER_H
#define SODIUM_LOGGER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

/**
 * Initialize the global logger.
 * @param filepath Path to the log file (defaults to "sodium.log" if NULL).
 * @return true on success, false on failure.
 */
bool logger_init(const char* filepath);

/**
 * Log a formatted message with timestamp, level, and subsystem tag.
 * Thread-safe and auto-flushing.
 */
void logger_log(LogLevel level, const char* component, const char* format, ...);

#define LOG_DEBUG(comp, ...) logger_log(LOG_LEVEL_DEBUG, comp, __VA_ARGS__)
#define LOG_INFO(comp, ...)  logger_log(LOG_LEVEL_INFO,  comp, __VA_ARGS__)
#define LOG_WARN(comp, ...)  logger_log(LOG_LEVEL_WARN,  comp, __VA_ARGS__)
#define LOG_ERROR(comp, ...) logger_log(LOG_LEVEL_ERROR, comp, __VA_ARGS__)

/**
 * Returns the active log filepath.
 */
const char* logger_get_filepath(void);

/**
 * Flushes the log file buffer immediately to disk.
 */
void logger_flush(void);

/**
 * Cleanly closes the log file.
 */
void logger_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_LOGGER_H
