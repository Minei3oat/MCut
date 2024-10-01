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
    bool is_keyframe;
    bool is_corrupt;
    AVPictureType frame_type;
} frame_info_t;

class MediaFile
{
public:
    MediaFile(std::string filename);
    ~MediaFile();

    AVFrame* get_frame(ssize_t frame_index);

    ssize_t find_iframe_before(ssize_t search);
    ssize_t find_pframe_before(ssize_t search);
    ssize_t find_iframe_after(ssize_t search);
    ssize_t find_pframe_after(ssize_t search);

    ssize_t get_frame_count() { return frame_count; }
    int get_reorder_length() { return reorder_length; }
    int get_max_bframes() { return max_bframes; }

    frame_info_t* get_frame_info(ssize_t frame_index);
    AVFormatContext* get_format_context() { return format_context; }
    AVStream* get_video_stream() { return video_stream; }

private:
    void cache_frame_infos();

    std::string filename;
    AVFormatContext *format_context = NULL;
    frame_info_t *frame_infos = NULL;
    unsigned long frame_count = 0;
    int reorder_length = 0;
    int max_bframes = 0;

    // temporary
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;
    AVStream *audio_stream2 = NULL;
    AVStream *subtitle_stream = NULL;
};

#endif // MEDIAFILE_H
