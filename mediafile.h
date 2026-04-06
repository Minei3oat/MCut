/*
 * SPDX-FileCopyrightText: 2023-2026 Minei3oat
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MEDIAFILE_H
#define MEDIAFILE_H

#include <string>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

typedef struct {
    unsigned long offset;
    long pts;
    long dts;
    unsigned int duration;
    bool is_keyframe;
    bool is_corrupt;
    char frame_type;
} packet_info_t;

typedef struct {
    ssize_t num_infos;
    packet_info_t* infos;
    packet_info_t* infos_end;
} stream_info_t;

class MediaFile
{
public:
    MediaFile(const std::string& filename);
    ~MediaFile();

    int seek(ssize_t frame_index);
    AVFrame* get_frame(ssize_t frame_index);

    ssize_t find_iframe_before(ssize_t search) const;
    ssize_t find_pframe_before(ssize_t search) const;
    ssize_t find_iframe_after(ssize_t search) const;
    ssize_t find_pframe_after(ssize_t search) const;

    ssize_t offset_before_pts(int64_t pts) const;
    ssize_t offset_after_pts(int64_t pts) const;

    ssize_t get_frame_count() const { return stream_infos[video_stream->index].num_infos; }
    ssize_t get_stream_count() const { return format_context->nb_streams; }
    int get_reorder_length() const { return reorder_length; }
    int get_max_bframes() const { return max_bframes; }
    int get_gop_size() const { return gop_size; }
    int get_max_difference() const { return max_difference; }

    const std::string& get_filename() const { return filename; }
    const packet_info_t* get_frame_info(ssize_t frame_index) const;
    const packet_info_t* get_packet_info(int stream_index, int64_t pts) const;
    const AVStream* get_video_stream() const { return video_stream; }
    AVCodecContext* get_video_decode_context(bool hw_accel = false);
    const AVStream* get_stream(size_t index) const;

    int next_packet(AVPacket* packet);

    bool is_audio_stream(int stream_index) const;

    ssize_t current_frame = 0;

private:
    void build_cache();
    void detect_hardware_decoding();

    AVFrame* get_raw_frame(ssize_t frame_index);

    std::string filename;
    AVFormatContext *format_context = NULL;
    AVCodecContext *codec_context = NULL;
    const AVCodecHWConfig *hw_config = NULL;
    int reorder_length = 0;
    int max_bframes = 0;
    int gop_size = 0;
    ssize_t filesize = 0;
    int64_t max_difference = 0;

    stream_info_t* stream_infos = NULL;

    // temporary
    AVStream *video_stream = NULL;
};

#endif // MEDIAFILE_H
