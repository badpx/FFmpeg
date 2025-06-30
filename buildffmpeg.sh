#!/bin/bash

if [ $# -eq 0 ]; then
    echo "错误：请指定构建安装的目标目录" >&2  # 输出到标准错误
    echo "如果要使用相对路径请以\$PWD开头" >&2  # --prefix只能接受绝对路径
    exit 1  # 返回非零状态码表示错误
fi

# 一次性清掉旧配置
make distclean 2>/dev/null || true

# 最小化配置
emconfigure ./configure \
    --prefix=$1          \
    --target-os=none \
    --arch=x86_32 \
    --enable-cross-compile \
    --disable-x86asm \
    --disable-inline-asm \
    --disable-programs \
    --disable-everything             \
    --disable-debug --disable-doc   \
    --enable-small                   \
    --enable-avcodec --enable-avformat \
    --enable-swresample              \
    --enable-protocol=file           \
    --enable-libmp3lame \
    --enable-encoder=libmp3lame \
    --enable-muxer=wav,mp3,adts      \
    --enable-demuxer=wav,mp3,aac     \
    --enable-decoder=aac,mp3,pcm_s16le \
    --enable-parser=aac,mp3          \
    --ar=emar \
    --ranlib=emranlib \
    --cc=emcc \
    --cxx=em++ \
    --extra-cxxflags="-O2 -DNDEBUG"     \
    --extra-cflags="-I$1/include -O2 -DNDEBUG"  \
    --extra-ldflags="-L$1/lib -lmp3lame"
# 编译 & 安装
emmake make -j$(sysctl -n hw.logicalcpu)
emmake make install

