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
    MediaFile(std::string filename);
    ~MediaFile();

    AVFrame* get_frame(ssize_t frame_index);

    ssize_t find_iframe_before(ssize_t search) const;
    ssize_t find_pframe_before(ssize_t search) const;
    ssize_t find_iframe_after(ssize_t search) const;
    ssize_t find_pframe_after(ssize_t search) const;

    ssize_t offset_before_pts(int64_t pts) const;
    ssize_t offset_after_pts(int64_t pts) const;

    ssize_t get_frame_count() { return !stream_infos ? NULL : stream_infos[video_stream->index].num_infos; }
    int get_reorder_length() { return reorder_length; }
    int get_max_bframes() { return max_bframes; }
    int get_gop_size() { return gop_size; }

    const std::string& get_filename() { return filename; }
    const packet_info_t* get_frame_info(ssize_t frame_index) const;
    const packet_info_t* get_packet_info(int stream_index, int64_t pts) const;
    AVFormatContext* get_format_context() { return format_context; }
    AVStream* get_video_stream() { return video_stream; }

    ssize_t current_frame = 0;

private:
    void build_cache();

    std::string filename;
    AVFormatContext *format_context = NULL;
    int reorder_length = 0;
    int max_bframes = 0;
    int gop_size = 0;
    ssize_t filesize = 0;

    stream_info_t* stream_infos = NULL;

    // temporary
    AVStream *video_stream = NULL;
};

#endif // MEDIAFILE_H
