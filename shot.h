#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include "queue.h"

typedef struct FilterContext {
    AVFilterContext *buffersrc_ctx;
    AVFilterContext *buffersink_ctx;
    AVFilterGraph *filter_graph;
} FilterContext;


typedef struct ShotContext {
    AVFormatContext *iformat_ctx;
    AVFormatContext *oformat_ctx;
    AVCodecContext *decodec_ctx;
    AVCodecContext *encodec_ctx;
    FilterContext *filter_ctx;
    char *url;
    char *codec_name;
    int video_stream_index;
    Queue *frames;
    Queue *filtered_frames;
    Queue *packets;
    AVDictionary *options;
} ShotContext;

int shot(const char *url, const char *codec_name, const char *output, int timeout);

ShotContext *open_shot_context(const char *url, const char *codec_name, const char *output, int timeout);

void close_shot_context(ShotContext *shot_ctx);

int open_iformat_context(char *filename, AVFormatContext **format_ctx, AVDictionary **options, int *video_stream_index);

int open_oformat_context(const char *filename, AVCodecContext *encodec_ctx, AVFormatContext **format_ctx);


int open_decodec_context(AVFormatContext *format_ctx, int stream_index, AVCodecContext **codec_ctx);

int open_encodec_context(const char *codec_name, AVCodecContext *decodec_ctx, AVCodecContext **codec_ctx);

int open_filter_context(AVCodecContext *decodec_ctx, AVCodecContext *encodec_ctx, FilterContext **filter_ctx,
                        const char *filter_spec);

int transcode_packet(ShotContext *transcode_ctx, AVPacket *packet);

int decode_packet(ShotContext *transcode_ctx, AVPacket *packet);

int filter_packet(ShotContext *transcode_ctx, AVFrame *frame);

int encode_packet(ShotContext *transcode_ctx, AVFrame *frame);

void mux_oformat_packets(ShotContext *transcode_ctx);