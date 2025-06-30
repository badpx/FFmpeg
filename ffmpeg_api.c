#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>


// 获取版本信息
unsigned int get_avcodec_version() {
    return avcodec_version();
}

char* get_version_str() {
    static char version_str[32];
    unsigned int version = avcodec_version();
    // 将版本号转换为字符串格式 (major.minor.micro)
    int major = (version >> 16) & 0xFF;
    int minor = (version >> 8) & 0xFF; 
    int micro = version & 0xFF;
    snprintf(version_str, sizeof(version_str), "%d.%d.%d", major, minor, micro);
    return version_str;
}

