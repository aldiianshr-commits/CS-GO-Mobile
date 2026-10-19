#include "common.h"
#include <cstdarg>
#include <cstdio>
#include <mutex>

static std::mutex g_logMutex;
static FILE* g_logFile = nullptr;

void csgo_set_log_file(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) { fclose(g_logFile); g_logFile = nullptr; }
    if (!path.empty()) g_logFile = fopen(path.c_str(), "w");
}

void csgo_log(int level, const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#ifdef __ANDROID__
    int prio = level == 1 ? ANDROID_LOG_INFO : level == 2 ? ANDROID_LOG_WARN : ANDROID_LOG_ERROR;
    __android_log_write(prio, LOG_TAG, buf);
#else
    fprintf(level >= 3 ? stderr : stdout, "[%c] %s\n", level == 1 ? 'I' : level == 2 ? 'W' : 'E', buf);
#endif
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) {
        fprintf(g_logFile, "[%c] %s\n", level == 1 ? 'I' : level == 2 ? 'W' : 'E', buf);
        fflush(g_logFile);
    }
}
