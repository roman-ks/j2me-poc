#pragma once

#ifdef ESP32_BUILD
#include <Arduino.h>
#define printf Serial.printf
#else
#include <cstdio>
#endif
// #include <typeinfo>
#include <cstring>

#ifndef LOG_LEVEL
#define LOG_LEVEL 0 // 0 - TRACE, 1 - DEBUG, 2 - INFO, 3 - WARN, 4 - ERROR
#endif

inline const char* normalizePath(const char* path){
    static char buf[512];
    size_t len = strlen(path) + 1;
    if(len >= sizeof(buf)) len = sizeof(buf)-1;
    for(size_t i=0; i<len; i++){
        buf[i] = path[i];
        if(buf[i] == '\\') buf[i] = '/';
    }
    return buf;
}

#define FILE_RELATIVE(file) \
    (strstr(normalizePath(file), "/src/") ? (strstr(normalizePath(file), "/src/") + 5) : file)

inline const char* createLoggerName(const char *file, long line){
    static char buf[512];
    sprintf(buf, "[%s:%ld]", FILE_RELATIVE(file), line);
    return buf;
}


#define LOGF(fmt, ...) \
    do { \
        printf("%-40s " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)

#define LOG(msg) \
    do { \
        printf("%-40s " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)

#if LOG_LEVEL == 0
#define LOGF_T(fmt, ...) \
    do { \
        printf("%-40s [TRACE] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)
#define LOG_T(msg) \
    do { \
        printf("%-40s [TRACE] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)

#define LOGF_D(fmt, ...) \
    do { \
        printf("%-40s [DEBUG] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)
#define LOG_D(msg) \
    do { \
        printf("%-40s [DEBUG] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
    
#define LOGF_I(fmt, ...) \
    do { \
        printf("%-40s [INFO ] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)

#define LOG_I(msg) \
    do { \
        printf("%-40s [INFO ] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
#define LOGF_W(fmt, ...) \
    do { \
        printf("%-40s [WARN ] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)

#define LOG_W(msg) \
    do { \
        printf("%-40s [WARN ] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
#elif LOG_LEVEL == 1 
#define LOGF_T(fmt, ...)
#define LOG_T(msg)
#define LOGF_D(fmt, ...) \
    do { \
        printf("%-40s [DEBUG] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)
#define LOG_D(msg) \
    do { \
        printf("%-40s [DEBUG] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
    
#define LOGF_I(fmt, ...) \
    do { \
        printf("%-40s [INFO ] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)

#define LOG_I(msg) \
    do { \
        printf("%-40s [INFO ] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
#define LOGF_W(fmt, ...) \
    do { \
        printf("%-40s [WARN ] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)

#define LOG_W(msg) \
    do { \
        printf("%-40s [WARN ] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
#elif LOG_LEVEL == 2 
#define LOGF_T(fmt, ...)
#define LOG_T(msg)
#define LOGF_D(fmt, ...)
#define LOG_D(msg)
#define LOGF_I(fmt, ...) \
    do { \
        printf("%-40s [INFO ] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)

#define LOG_I(msg) \
    do { \
        printf("%-40s [INFO ] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
#define LOGF_W(fmt, ...) \
    do { \
        printf("%-40s [WARN ] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)

#define LOG_W(msg) \
    do { \
        printf("%-40s [WARN ] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
#elif LOG_LEVEL == 3 
#define LOGF_T(fmt, ...)
#define LOG_T(msg)
#define LOGF_D(fmt, ...)
#define LOG_D(msg)
#define LOGF_I(fmt, ...)
#define LOG_I(msg)
#define LOGF_W(fmt, ...) \
    do { \
        printf("%-40s [WARN ] " fmt "\n", createLoggerName(__FILE__, __LINE__), ##__VA_ARGS__); \
    } while(0)

#define LOG_W(msg) \
    do { \
        printf("%-40s [WARN ] " msg "\n", createLoggerName(__FILE__, __LINE__)); \
    } while(0)
#endif

#define LOG_ARRAY(fmt,arr,size)\
    do { \
        printf("%-40s [ ", createLoggerName(__FILE__, __LINE__)); \
        for (int i = 0; i < size; i++) { \
            printf(fmt ", ", arr[i]); \
        } \
        printf("]\n"); \
    } while (0)
