#!/bin/bash

if [ $# -eq 0 ]; then
    echo "错误：请指定依赖库的安装目录" >&2  # 输出到标准错误
    exit 1  # 返回非零状态码表示错误
fi

emcc \
    -I$1/include \
    -L$1/lib  \
    ffmpeg_api.c \
    -lavformat -lavcodec -lswresample -lavutil -lm \
    -Wl,--no-entry  \
    -Wl,--export=get_avcodec_version    \
    -s STANDALONE_WASM=1    \
    -s ASSERTIONS=1 \
    -O1 \
    -o ffmpeg.wasm

