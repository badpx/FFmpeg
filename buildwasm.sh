
emcc \
    -I./build/include/ \
    -L./build/lib/  \
    ffmpeg_api.c \
    -lavformat -lavcodec -lswresample -lavutil -lm \
    -Wl,--no-entry  \
    -Wl,--export=get_avcodec_version    \
    -s STANDALONE_WASM=1    \
    -s ASSERTIONS=1 \
    -O1 \
    -o ffmpeg.wasm

