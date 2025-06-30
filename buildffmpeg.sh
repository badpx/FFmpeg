# 一次性清掉旧配置
make distclean 2>/dev/null || true

# 最小化配置
emconfigure ./configure \
    --prefix=$PWD/build          \
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
    --enable-demuxer=wav,mp3,aac     \
    --enable-muxer=wav,mp3,adts      \
    --enable-decoder=aac,mp3,pcm_s16le \
    --enable-parser=aac,mp3          \
    --ar=emar \
    --ranlib=emranlib \
    --cc=emcc \
    --cxx=em++ \
    --extra-cflags="-O2 -DNDEBUG" \
    --extra-cxxflags="-O2 -DNDEBUG"

# 编译 & 安装
emmake make -j$(sysctl -n hw.logicalcpu)
emmake make install

