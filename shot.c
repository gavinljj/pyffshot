#include "shot.h"


/**
 * 从视频中获取第一个关键帧作为视频截图
 * @param url
 * @param codec_name 图片编码名称
 * @param output 图片保存路径
 * @return
 */
int shot(const char *url, const char *codec_name, const char *output, int timeout) {
    ShotContext *shot_ctx = open_shot_context(url, codec_name, output, timeout);
    if (shot_ctx == NULL) {
        printf("open shot context error\n");
        return -1;
    }
    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;
    while (av_read_frame(shot_ctx->iformat_ctx, &packet) >= 0) {
        if (packet.stream_index == shot_ctx->video_stream_index) {
            av_packet_rescale_ts(&packet,
                                 shot_ctx->iformat_ctx->streams[packet.stream_index]->time_base,
                                 shot_ctx->decodec_ctx->time_base);

            transcode_packet(shot_ctx, &packet);
            if (!is_empty_queue(shot_ctx->packets)) {
                mux_oformat_packets(shot_ctx);
                av_packet_unref(&packet);
                close_shot_context(shot_ctx);
                return 0;
            }
        }
    }
    av_packet_unref(&packet);
    close_shot_context(shot_ctx);
    return -1;
}


/**
 * 打开截图上下文
 * @param url
 * @param codec_name
 * @param output
 * @return
 */
ShotContext *open_shot_context(const char *url, const char *codec_name, const char *output, int timeout) {
    ShotContext *shot_ctx = (ShotContext *) malloc(sizeof(ShotContext));
    if (shot_ctx == NULL) {
        printf("av_mallocz_array failed\n");
        return NULL;
    }
    shot_ctx->codec_name = codec_name;
    shot_ctx->url = url;
    shot_ctx->options = NULL;
    if (timeout > 0) {
        av_dict_set_int(&(shot_ctx->options), "timeout", timeout * 1000, 0);
    }// 打开input AVFormatContext
    int video_stream_index;
    AVFormatContext *iformat_ctx = NULL;
    if (open_iformat_context(url, &iformat_ctx, &(shot_ctx->options), &video_stream_index) < 0) {
        printf("open_iformat_context failed\n");
        return NULL;
    }
    shot_ctx->iformat_ctx = iformat_ctx;
    shot_ctx->video_stream_index = video_stream_index;
    // 打开解码 AVCodecContext
    AVCodecContext *decodec_ctx = NULL;
    if (open_decodec_context(iformat_ctx, video_stream_index, &decodec_ctx) < 0) {
        printf("open deocodec context failed\n");
        return NULL;
    }
    shot_ctx->decodec_ctx = decodec_ctx;


    // 打开编码AVCodecContext
    AVCodecContext *encodec_ctx = NULL;
    if (open_encodec_context(shot_ctx->codec_name, decodec_ctx, &encodec_ctx) < 0) {
        printf("open encodec context failed\n");
        return NULL;
    }
    shot_ctx->encodec_ctx = encodec_ctx;


    FilterContext *filter_ctx = NULL;
    if (open_filter_context(decodec_ctx, encodec_ctx, &filter_ctx,
                            decodec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ? "null" : "anull") < 0) {
        printf("open_filter_context failed\n");
        return NULL;
    }
    shot_ctx->filter_ctx = filter_ctx;

    AVFormatContext *oformat_ctx = NULL;
    if (open_oformat_context(output, encodec_ctx, &oformat_ctx) < 0) {
        printf("open_oformat_context failed\n ");
        return NULL;
    }
    shot_ctx->oformat_ctx = oformat_ctx;

    shot_ctx->frames = create_queue();
    shot_ctx->filtered_frames = create_queue();
    shot_ctx->packets = create_queue();

    return shot_ctx;
}

void close_shot_context(ShotContext *shot_ctx) {
    if (shot_ctx->oformat_ctx && !(shot_ctx->oformat_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&(shot_ctx->oformat_ctx->pb));
    }
    avcodec_free_context(&(shot_ctx->decodec_ctx));
    avcodec_free_context(&(shot_ctx->encodec_ctx));
    avfilter_graph_free(&(shot_ctx->filter_ctx->filter_graph));
    free(shot_ctx->filter_ctx);
    avformat_free_context(shot_ctx->oformat_ctx);
    avformat_close_input(&(shot_ctx->iformat_ctx));
    while(!is_empty_queue(shot_ctx->frames)) {
        av_frame_free(pop_queue(shot_ctx->frames));
    }
    while (!is_empty_queue(shot_ctx->filtered_frames)) {
        av_frame_free(pop_queue(shot_ctx->filtered_frames));
    }
    while (!is_empty_queue(shot_ctx->packets)) {
        av_packet_free(pop_queue(shot_ctx->packets));
    }
    destroy_queue(shot_ctx->frames);
    destroy_queue(shot_ctx->filtered_frames);
    destroy_queue(shot_ctx->packets);
    if (shot_ctx->options) {
        av_dict_free(&(shot_ctx->options));
    }
    free(shot_ctx);
}


/**
 * 打开input AVFormatContext，并定位video stream
 * @param filename
 * @param format_ctx
 * @param video_stream
 * @return
 */
int open_iformat_context(char *filename, AVFormatContext **format_ctx, AVDictionary **options, int *video_stream) {
    int ret;
    if ((ret = avformat_open_input(format_ctx, filename, NULL, options)) < 0) {
        printf("avformat_open_input failed, %s\n", av_err2str(ret));
        return ret;
    }
    if ((ret = avformat_find_stream_info(*format_ctx, NULL)) < 0) {
        printf("avformat_find_stream_info failed, %s\n", av_err2str(ret));
        return ret;
    }

    int video_stream_index = -1, i = 0;
    for (i = 0; i < (*format_ctx)->nb_streams; ++i) {
        if ((*format_ctx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index < 0 || video_stream_index > (*format_ctx)->nb_streams) {
        printf("no video stream found\n");
        return -1;
    }
    *video_stream = video_stream_index;

    return 0;
}


/**
 * 打开解码上下文
 * @param codec_id 解码器id
 * @param codecpar 解码参数
 * @param decodec_ctx 返回的解码上下文
 * @return
 */
int open_decodec_context(AVFormatContext *format_ctx, int stream_index, AVCodecContext **decodec_ctx) {
    AVCodec *codec = NULL;
    int ret;
    codec = avcodec_find_decoder(format_ctx->streams[stream_index]->codecpar->codec_id);
    if (!codec) {
        printf("avcodec_find_decoder failed\n");
        return -1;
    }
    if (!(*decodec_ctx = avcodec_alloc_context3(codec))) {
        printf("avcodec_alloc_context3 failed\n");
        return -1;
    }
    if ((ret = avcodec_parameters_to_context(*decodec_ctx, format_ctx->streams[stream_index]->codecpar)) < 0) {
        printf("avcodec_parameters_to_context failed, %s\n", av_err2str(ret));
        return -1;
    }
    if ((*decodec_ctx)->codec_type == AVMEDIA_TYPE_VIDEO) {
        (*decodec_ctx)->framerate = av_guess_frame_rate(format_ctx, format_ctx->streams[stream_index], NULL);
    }
    if ((ret = avcodec_open2(*decodec_ctx, codec, NULL)) < 0) {
        printf("avcodec_open2 failed, %s\n", av_err2str(ret));
        return -1;
    }
    if ((*decodec_ctx)->codec_type == AVMEDIA_TYPE_AUDIO && !(*decodec_ctx)->channel_layout) {
        (*decodec_ctx)->channel_layout = av_get_default_channel_layout((*decodec_ctx)->channels);
    }
    return 0;
}


/**
 * 打开编码上下文
 * @param codec_id 编解码器id
 * @param codecpar 编码参数
 * @param encodec_ctx 返回的编码上下文
 * @return
 */
int open_encodec_context(const char *codec_name, AVCodecContext *decodec_ctx, AVCodecContext **encodec_ctx) {
    AVCodec *codec = NULL;
    int ret;
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        printf("avcodec_find_encoder failed\n");
        return -1;
    }
    if (!(*encodec_ctx = avcodec_alloc_context3(codec))) {
        printf("avcodec_alloc_context3 failed\n");
        return -1;
    }
    if ((*encodec_ctx)->codec_type == AVMEDIA_TYPE_VIDEO) { // 设置视频编码参数
        (*encodec_ctx)->width = decodec_ctx->width;
        (*encodec_ctx)->height = decodec_ctx->height;
        (*encodec_ctx)->sample_aspect_ratio = decodec_ctx->sample_aspect_ratio;
        if (codec->pix_fmts) {
            (*encodec_ctx)->pix_fmt = codec->pix_fmts[0];
        } else {
            (*encodec_ctx)->pix_fmt = decodec_ctx->pix_fmt;
        }
        (*encodec_ctx)->time_base = av_inv_q(decodec_ctx->framerate);
    } else if ((*encodec_ctx)->codec_type == AVMEDIA_TYPE_AUDIO) { // 设置音频编码参数
        (*encodec_ctx)->sample_rate = decodec_ctx->sample_rate;
        (*encodec_ctx)->channel_layout = decodec_ctx->channel_layout;
        (*encodec_ctx)->channels = av_get_channel_layout_nb_channels(decodec_ctx->channel_layout);
        (*encodec_ctx)->sample_fmt = codec->sample_fmts[0];
        (*encodec_ctx)->time_base = (AVRational) {1, decodec_ctx->sample_rate};
    }
    if ((ret = avcodec_open2(*encodec_ctx, codec, NULL)) < 0) {
        printf("avcodec_open2 failed, %s\n", av_err2str(ret));
        return -1;
    }
    return 0;
}


/**
 * 打开音视频帧过滤上下文
 * @param decodec_ctx
 * @param filter_ctx
 * @return
 */
int open_filter_context(AVCodecContext *decodec_ctx, AVCodecContext *encodec_ctx, FilterContext **filter_ctx,
                        const char *filter_spec) {
    const AVFilter *buffersrc, *buffersink;
    AVFilterContext *buffersrc_ctx = NULL, *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc(), *inputs = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = NULL;
    int ret = 0;
    char args[512];
    if (!outputs || !inputs) {
        printf("avfilter_inout_alloc failed\n");
        ret = -1;
        goto end;
    }
    *filter_ctx = (FilterContext *) malloc(sizeof(**filter_ctx));
    if (!*filter_ctx) {
        printf("malloc FilterContext failed\n");
        ret = -1;
        goto end;
    }
    if (decodec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        snprintf(args, sizeof(args),
                 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 decodec_ctx->width, decodec_ctx->height, decodec_ctx->pix_fmt,
                 decodec_ctx->time_base.num, decodec_ctx->time_base.den,
                 decodec_ctx->sample_aspect_ratio.num, decodec_ctx->sample_aspect_ratio.den);
    } else {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        snprintf(args, sizeof(args),
                 "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
                 decodec_ctx->time_base.num, decodec_ctx->time_base.den,
                 decodec_ctx->sample_rate, av_get_sample_fmt_name(decodec_ctx->sample_fmt),
                 decodec_ctx->channel_layout);
    }
    if (!buffersrc || !buffersink) {
        printf("avfilter_get_by_name failed\n");
        ret = -1;
        goto end;
    }
    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        printf("avfilter_graph_alloc failed\n");
        ret = -1;
        goto end;
    }

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
    if (ret < 0) {
        printf("avfilter_graph_create_filter failed, %s\n", av_err2str(ret));
        goto end;
    }
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
    if (ret < 0) {
        printf("avfilter_graph_create_filter failed, %s\n", av_err2str(ret));
        goto end;
    }
    if (decodec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts", (uint8_t * ) & encodec_ctx->pix_fmt,
                             sizeof(encodec_ctx->pix_fmt), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            printf("av_op_set_bin set pix_fmts failed, %s\n", av_err2str(ret));
            goto end;
        }
    } else {
        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts", (uint8_t * ) & decodec_ctx->sample_fmt,
                             sizeof(decodec_ctx->sample_fmt), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            printf("av_op_set_bin set pix_fmts failed, %s\n", av_err2str(ret));
            goto end;
        }
        ret = av_opt_set_bin(buffersink_ctx, "channel_layouts", (uint8_t * ) & decodec_ctx->channel_layout,
                             sizeof(decodec_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            printf("av_op_set_bin set channel_layouts failed, %s\n", av_err2str(ret));
            goto end;
        }
        ret = av_opt_set_bin(buffersink_ctx, "sample_rates", (uint8_t * ) & decodec_ctx->sample_rate,
                             sizeof(decodec_ctx->sample_rate), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            printf("av_op_set_bin set sample_rates failed, %s\n", av_err2str(ret));
            goto end;
        }
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if (!outputs->name || !inputs->name) {
        printf("av_strdup failed\n");
        ret = -1;
        goto end;
    }
    ret = avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs, &outputs, NULL);
    if (ret < 0) {
        printf("avfilter_graph_parse_ptr failed\n");
        goto end;
    }
    ret = avfilter_graph_config(filter_graph, NULL);
    if (ret < 0) {
        printf("avfilter_graph_config failed, %s\n", av_err2str(ret));
        goto end;
    }

    (*filter_ctx)->buffersrc_ctx = buffersrc_ctx;
    (*filter_ctx)->buffersink_ctx = buffersink_ctx;
    (*filter_ctx)->filter_graph = filter_graph;
    end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

/**
 * 打开输出文件的AVFormatContext，并初始化相应的AVStream
 * @param filename 输出文件名
 * @param nb_streams 流数
 * @param shot_ctx 流转码上下文数组
 * @param format_ctx 返回的AVFormatContext
 * @return
 */
int open_oformat_context(const char *filename, AVCodecContext *encodec_ctx,
                         AVFormatContext **format_ctx) {
    AVStream *stream;
    int ret;
    avformat_alloc_output_context2(format_ctx, NULL, NULL, filename);
    if (!(*format_ctx)) {
        printf("avformat_alloc_context2 failed\n");
        return -1;
    }
    stream = avformat_new_stream(*format_ctx, NULL);
    if (!stream) {
        printf("avformat_new_stream failed\n");
        return -1;
    }
    ret = avcodec_parameters_from_context(stream->codecpar, encodec_ctx);
    if (ret < 0) {
        printf("avodec_parameters_from_context failed, %s\n", av_err2str(ret));
        return ret;
    }
    if ((*format_ctx)->oformat->flags & AVFMT_GLOBALHEADER) {
        encodec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    stream->time_base = encodec_ctx->time_base;
    av_dump_format(*format_ctx, 0, filename, 1);
    if (!((*format_ctx)->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&(*format_ctx)->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            printf("avio_open failed, %s\n", av_err2str(ret));
            return ret;
        }
    }
    ret = avformat_write_header(*format_ctx, NULL);
    if (ret < 0) {
        printf("avformat_write_header failed, %s\n", av_err2str(ret));
        return ret;
    }
    return 0;
}


/**
 * 转码一帧
 * @param shot_ctx
 * @param packet
 * @return
 */
int transcode_packet(ShotContext *shot_ctx, AVPacket *packet) {
    if (shot_ctx->decodec_ctx &&
        shot_ctx->encodec_ctx) {
        if (decode_packet(shot_ctx, packet) < 0) {
            printf("stream-%d transcode a packet failed\n", packet->stream_index);
            return -1;
        }
        AVFrame *frame;
        while (!is_empty_queue(shot_ctx->frames)) {
            frame = (AVFrame *) pop_queue(shot_ctx->frames);
            if (filter_packet(shot_ctx, frame) < 0) {
                printf("stream-%d filter_packet failed\n", packet->stream_index);
                return -1;
            }
        }
        while (!is_empty_queue(shot_ctx->filtered_frames)) {
            if (encode_packet(shot_ctx, (AVFrame *) pop_queue(shot_ctx->filtered_frames)) < 0) {
                printf("stream-%d encode_packet failed\n", packet->stream_index);
                return -1;
            }
        }
    }
    return -1;
}


/**
 * 解码一帧
 * @param shot_ctx
 * @param packet
 * @return
 */
int decode_packet(ShotContext *shot_ctx, AVPacket *packet) {
    int ret;
    if ((ret = avcodec_send_packet(shot_ctx->decodec_ctx, packet)) < 0) {
        printf("avcodec_send_packet failed, %s\n", av_err2str(ret));
        return ret;
    }
    AVFrame *frame = NULL;
    while (ret >= 0) {
        frame = av_frame_alloc();
        if (!frame) {
            printf("av_frame_alloc failed\n");
            return -1;
        }
        ret = avcodec_receive_frame(shot_ctx->decodec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            printf("avcodec_receive_frame failed, %s\n", av_err2str(ret));
            return ret;
        } else if (frame->key_frame == 1) {
            frame->pts = frame->best_effort_timestamp;
            push_queue(shot_ctx->frames, frame);
        }
    }
    return 0;
}


/**
 * 过滤一帧
 * @param shot_ctx
 * @param frame
 * @return
 */
int filter_packet(ShotContext *shot_ctx, AVFrame *frame) {
    int ret;
    AVFrame *filtered_frame = NULL;
    if (!shot_ctx->filter_ctx) {
        ret = -1;
        goto end;
    }
    ret = av_buffersrc_add_frame_flags(shot_ctx->filter_ctx->buffersrc_ctx, frame, 0);
    if (ret < 0) {
        printf("av_buffersrc_add_frame_flags failed, %s\n", av_err2str(ret));
        goto end;
    }
    while (true) {
        filtered_frame = av_frame_alloc();
        if (!filtered_frame) {
            printf("av_frame_alloc failed\n");
            ret = -1;
            goto end;
        }
        ret = av_buffersink_get_frame(shot_ctx->filter_ctx->buffersink_ctx, filtered_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 0;
            goto end;
        } else if (ret < 0) {
            printf("av_buffersink_get_frame failed, %s\n", av_err2str(ret));
            goto end;
        } else {
            filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;
            push_queue(shot_ctx->filtered_frames, filtered_frame);
        }

    }
    end:
    av_frame_free(&frame);
    return ret;
}


/**
 * 编码一帧
 * @param shot_ctx
 * @param frame
 * @return
 */
int encode_packet(ShotContext *shot_ctx, AVFrame *frame) {
    int ret;
    if ((ret = avcodec_send_frame(shot_ctx->encodec_ctx, frame)) < 0) {
        printf("avodec_send_frame failed, %s\n", av_err2str(ret));
        goto end;
    }
    AVPacket *packet = NULL;
    while (ret >= 0) {
        packet = av_packet_alloc();
        if (!packet) {
            printf("av_packet_alloc failed\n");
            ret = -1;
            goto end;
        }
        av_init_packet(packet);
        packet->data = NULL;
        packet->size = 0;
        ret = avcodec_receive_packet(shot_ctx->encodec_ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 0;
            goto end;
        } else if (ret < 0) {
            printf("avcodec_receive_packet failed, %s\n", av_err2str(ret));
            goto end;
        } else {
            push_queue(shot_ctx->packets, packet);
        }
    }
    end:
    av_frame_free(&frame);
    return ret;
}


void mux_oformat_packets(ShotContext *shot_ctx) {
    int ret;
    AVPacket *packet = NULL;
    while (!is_empty_queue(shot_ctx->packets)) {
        packet = (AVPacket *) pop_queue(shot_ctx->packets);

        packet->stream_index = 0;
        av_packet_rescale_ts(packet,
                             shot_ctx->encodec_ctx->time_base,
                             shot_ctx->decodec_ctx->time_base);
        printf("Packet dts:%d, pts:%d, duration:%d, size:%d\n", packet->dts, packet->pts, packet->duration,
               packet->size);
        ret = av_interleaved_write_frame(shot_ctx->oformat_ctx, packet);
        av_write_trailer(shot_ctx->oformat_ctx);
        return;
    }
}

/**
 * 从input获取第一个关键帧，并编码为jpeg
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: ./shot INPUT OUTPUT\n");
        exit(-1);
    }

    char *infilename = argv[1], *outfilename = argv[2];
    AVFormatContext *ifmt_ctx = NULL, *oformat_ctx = NULL;
    shot(infilename, "mjpeg", outfilename, 0);
}
