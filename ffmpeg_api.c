#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

typedef struct {
    AVFormatContext *input_format_ctx;
    AVFormatContext *output_format_ctx;
    AVCodecContext *decoder_ctx;
    AVCodecContext *encoder_ctx;
    SwrContext *swr_ctx;
    int audio_stream_index;
} AudioConverter;

// 错误处理宏
#define CHECK_ERROR(ret, msg) \
    if (ret < 0) { \
        char errbuf[AV_ERROR_MAX_STRING_SIZE]; \
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE); \
        //fprintf(stderr, "Error %s: %s\n", msg, errbuf); \
        return ret; \
    }

int init_input(AudioConverter *converter, const char *input_filename) {
    int ret;
    
    // 打开输入文件
    ret = avformat_open_input(&converter->input_format_ctx, input_filename, NULL, NULL);
    CHECK_ERROR(ret, "opening input file");
    
    // 查找流信息
    ret = avformat_find_stream_info(converter->input_format_ctx, NULL);
    CHECK_ERROR(ret, "finding stream info");
    
    // 查找音频流
    converter->audio_stream_index = av_find_best_stream(
        converter->input_format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    
    if (converter->audio_stream_index < 0) {
        fprintf(stderr, "No audio stream found in input file\n");
        return converter->audio_stream_index;
    }
    
    // 获取音频流参数
    AVStream *audio_stream = converter->input_format_ctx->streams[converter->audio_stream_index];
    const AVCodec *decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!decoder) {
        fprintf(stderr, "Decoder not found\n");
        return AVERROR(EINVAL);
    }
    
    // 创建解码器上下文
    converter->decoder_ctx = avcodec_alloc_context3(decoder);
    if (!converter->decoder_ctx) {
        fprintf(stderr, "Failed to allocate decoder context\n");
        return AVERROR(ENOMEM);
    }
    
    // 从流中复制参数到解码器上下文
    ret = avcodec_parameters_to_context(converter->decoder_ctx, audio_stream->codecpar);
    CHECK_ERROR(ret, "copying parameters to decoder context");
    
    // 打开解码器
    ret = avcodec_open2(converter->decoder_ctx, decoder, NULL);
    CHECK_ERROR(ret, "opening decoder");
    
    return 0;
}

int init_output(AudioConverter *converter, const char *output_filename) {
    int ret;
    
    // 创建输出格式上下文
    ret = avformat_alloc_output_context2(&converter->output_format_ctx, NULL, "wav", output_filename);
    CHECK_ERROR(ret, "allocating output context");
    
    // 查找WAV编码器
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!encoder) {
        fprintf(stderr, "WAV encoder not found\n");
        return AVERROR(EINVAL);
    }
    
    // 创建编码器上下文
    converter->encoder_ctx = avcodec_alloc_context3(encoder);
    if (!converter->encoder_ctx) {
        fprintf(stderr, "Failed to allocate encoder context\n");
        return AVERROR(ENOMEM);
    }
    
    // 设置编码参数
    converter->encoder_ctx->sample_rate = converter->decoder_ctx->sample_rate;
    av_channel_layout_copy(&converter->encoder_ctx->ch_layout, &converter->decoder_ctx->ch_layout);
    converter->encoder_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    converter->encoder_ctx->bit_rate = 1411200; // 44.1kHz * 16bit * 2ch
    
    // 打开编码器
    ret = avcodec_open2(converter->encoder_ctx, encoder, NULL);
    CHECK_ERROR(ret, "opening encoder");
    
    // 创建输出流
    AVStream *output_stream = avformat_new_stream(converter->output_format_ctx, NULL);
    if (!output_stream) {
        fprintf(stderr, "Failed to create output stream\n");
        return AVERROR(ENOMEM);
    }
    
    // 复制编码器参数到输出流
    ret = avcodec_parameters_from_context(output_stream->codecpar, converter->encoder_ctx);
    CHECK_ERROR(ret, "copying parameters to output stream");
    
    // 打开输出文件
    if (!(converter->output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&converter->output_format_ctx->pb, output_filename, AVIO_FLAG_WRITE);
        CHECK_ERROR(ret, "opening output file");
    }
    
    // 写入文件头
    ret = avformat_write_header(converter->output_format_ctx, NULL);
    CHECK_ERROR(ret, "writing output header");
    
    return 0;
}

int init_resampler(AudioConverter *converter) {
    int ret;
    
    // 创建重采样上下文
    ret = swr_alloc_set_opts2(&converter->swr_ctx,
                              &converter->encoder_ctx->ch_layout,
                              converter->encoder_ctx->sample_fmt,
                              converter->encoder_ctx->sample_rate,
                              &converter->decoder_ctx->ch_layout,
                              converter->decoder_ctx->sample_fmt,
                              converter->decoder_ctx->sample_rate,
                              0, NULL);
    CHECK_ERROR(ret, "allocating resampler");
    
    // 初始化重采样器
    ret = swr_init(converter->swr_ctx);
    CHECK_ERROR(ret, "initializing resampler");
    
    return 0;
}

int convert_audio(AudioConverter *converter) {
    AVPacket *packet = av_packet_alloc();
    AVFrame *input_frame = av_frame_alloc();
    int ret = 0;
    
    if (!packet || !input_frame) {
        fprintf(stderr, "Failed to allocate packet or frames\n");
        ret = AVERROR(ENOMEM);
        goto cleanup;
    }
    
    // 读取和处理数据包
    while (av_read_frame(converter->input_format_ctx, packet) >= 0) {
        if (packet->stream_index == converter->audio_stream_index) {
            // 发送数据包到解码器
            ret = avcodec_send_packet(converter->decoder_ctx, packet);
            if (ret < 0) {
                fprintf(stderr, "Error sending packet to decoder\n");
                break;
            }
            
            // 从解码器接收帧
            while (ret >= 0) {
                ret = avcodec_receive_frame(converter->decoder_ctx, input_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    fprintf(stderr, "Error receiving frame from decoder\n");
                    goto cleanup;
                }
                
                // 计算重采样输出样本数
                int dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(converter->swr_ctx, converter->decoder_ctx->sample_rate) + input_frame->nb_samples,
                    converter->encoder_ctx->sample_rate,
                    converter->decoder_ctx->sample_rate,
                    AV_ROUND_UP);
                
                // 创建输出帧
                AVFrame *output_frame = av_frame_alloc();
                if (!output_frame) {
                    fprintf(stderr, "Failed to allocate output frame\n");
                    ret = AVERROR(ENOMEM);
                    goto cleanup;
                }
                
                output_frame->format = converter->encoder_ctx->sample_fmt;
                av_channel_layout_copy(&output_frame->ch_layout, &converter->encoder_ctx->ch_layout);
                output_frame->sample_rate = converter->encoder_ctx->sample_rate;
                output_frame->nb_samples = dst_nb_samples;
                
                ret = av_frame_get_buffer(output_frame, 0);
                if (ret < 0) {
                    fprintf(stderr, "Error allocating output frame buffer\n");
                    av_frame_free(&output_frame);
                    goto cleanup;
                }
                
                // 重采样
                ret = swr_convert(converter->swr_ctx,
                                output_frame->data, dst_nb_samples,
                                (const uint8_t**)input_frame->data, input_frame->nb_samples);
                if (ret < 0) {
                    fprintf(stderr, "Error during resampling\n");
                    av_frame_free(&output_frame);
                    goto cleanup;
                }
                
                output_frame->nb_samples = ret;
                output_frame->pts = av_rescale_q(input_frame->pts,
                                              converter->input_format_ctx->streams[converter->audio_stream_index]->time_base,
                                              (AVRational){1, converter->encoder_ctx->sample_rate});
                
                // 编码输出帧
                ret = avcodec_send_frame(converter->encoder_ctx, output_frame);
                av_frame_free(&output_frame);
                if (ret < 0) {
                    fprintf(stderr, "Error sending frame to encoder\n");
                    goto cleanup;
                }
                
                // 从编码器接收数据包
                AVPacket *output_packet = av_packet_alloc();
                while (ret >= 0) {
                    ret = avcodec_receive_packet(converter->encoder_ctx, output_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        fprintf(stderr, "Error receiving packet from encoder\n");
                        av_packet_free(&output_packet);
                        goto cleanup;
                    }
                    
                    // 写入输出文件
                    output_packet->stream_index = 0;
                    ret = av_interleaved_write_frame(converter->output_format_ctx, output_packet);
                    if (ret < 0) {
                        fprintf(stderr, "Error writing frame to output\n");
                        av_packet_free(&output_packet);
                        goto cleanup;
                    }
                    av_packet_unref(output_packet);
                }
                av_packet_free(&output_packet);
            }
        }
        av_packet_unref(packet);
    }
    
    // 刷新重采样器中剩余的样本
    AVFrame *flush_frame = av_frame_alloc();
    if (flush_frame) {
        int remaining_samples = swr_get_delay(converter->swr_ctx, converter->encoder_ctx->sample_rate);
        if (remaining_samples > 0) {
            flush_frame->format = converter->encoder_ctx->sample_fmt;
            av_channel_layout_copy(&flush_frame->ch_layout, &converter->encoder_ctx->ch_layout);
            flush_frame->sample_rate = converter->encoder_ctx->sample_rate;
            flush_frame->nb_samples = remaining_samples;
            
            if (av_frame_get_buffer(flush_frame, 0) >= 0) {
                ret = swr_convert(converter->swr_ctx,
                                flush_frame->data, remaining_samples,
                                NULL, 0);
                if (ret > 0) {
                    flush_frame->nb_samples = ret;
                    avcodec_send_frame(converter->encoder_ctx, flush_frame);
                }
            }
        }
        av_frame_free(&flush_frame);
    }
    
    // 刷新编码器
    avcodec_send_frame(converter->encoder_ctx, NULL);
    AVPacket *output_packet = av_packet_alloc();
    while (avcodec_receive_packet(converter->encoder_ctx, output_packet) >= 0) {
        output_packet->stream_index = 0;
        av_interleaved_write_frame(converter->output_format_ctx, output_packet);
        av_packet_unref(output_packet);
    }
    av_packet_free(&output_packet);
    
    // 写入文件尾
    av_write_trailer(converter->output_format_ctx);
    
cleanup:
    av_packet_free(&packet);
    av_frame_free(&input_frame);
    return ret;
}

void cleanup_converter(AudioConverter *converter) {
    if (converter->swr_ctx) {
        swr_free(&converter->swr_ctx);
    }
    if (converter->decoder_ctx) {
        avcodec_free_context(&converter->decoder_ctx);
    }
    if (converter->encoder_ctx) {
        avcodec_free_context(&converter->encoder_ctx);
    }
    if (converter->input_format_ctx) {
        avformat_close_input(&converter->input_format_ctx);
    }
    if (converter->output_format_ctx) {
        if (!(converter->output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&converter->output_format_ctx->pb);
        }
        avformat_free_context(converter->output_format_ctx);
    }
}

// 获取版本信息
unsigned int get_avcodec_version() {
    return avcodec_version();
}

