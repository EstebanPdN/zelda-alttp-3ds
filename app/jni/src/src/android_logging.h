#ifndef ANDROID_LOGGING_H
#define ANDROID_LOGGING_H

#include <android/log.h>
#include <stdio.h>

#define LOG_TAG "Zelda3"

#define fprintf(file, format, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, format, ##__VA_ARGS__)

#endif // ANDROID_LOGGING_H
