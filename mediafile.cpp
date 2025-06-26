#include "mediafile.h"

#include <algorithm>

#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>

// #define TRACE

MediaFile::MediaFile(std::string filename) : filename(filename)
{
    // get filesize
    struct stat info;
    stat(filename.c_str(), &info);
    filesize = info.st_size;

    // preparations
    format_context = avformat_alloc_context();

    // open file
    avformat_open_input(&format_context, filename.c_str(), NULL, NULL);
    printf("Format %s, duration %ld us\n", format_context->iformat->long_name, format_context->duration);

    // find streams
    avformat_find_stream_info(format_context,  NULL);

    // analyze streams
    for (int i = 0; i < format_context->nb_streams; i++)
    {
        AVStream* stream = format_context->streams[i];
        AVCodecParameters *local_codec_parameters = stream->codecpar;
        const AVCodec *local_codec = avcodec_find_decoder(local_codec_parameters->codec_id);
        if (local_codec == NULL) {
            printf("No codec found\n");
            continue;
        }

        // print stream info
        switch (local_codec_parameters->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                printf("Video Codec: resolution %d x %d\n", local_codec_parameters->width, local_codec_parameters->height);
                video_stream = stream;
                break;
            case AVMEDIA_TYPE_AUDIO:
                printf("Audio Codec: %d channels, sample rate %d\n", local_codec_parameters->ch_layout.nb_channels, local_codec_parameters->sample_rate);
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                printf("Subtitle Codec: %s\n", avcodec_get_name(local_codec_parameters->codec_id));
                break;
            default:
                printf("Unknown Codec: %s\n", avcodec_get_name(local_codec_parameters->codec_id));
                break;
        }
        // general
        printf("\tCodec %s ID %d bit_rate %ld\n", local_codec->long_name, local_codec->id, local_codec_parameters->bit_rate);
        printf("\tDuration %ld us; timebase: %d/%d\n", stream->duration,  stream->time_base.num, stream->time_base.den);
    }

    // build cache
    build_cache();
}

MediaFile::~MediaFile()
{
    for (int i = 0; i < format_context->nb_streams; i++) {
        munmap(stream_infos[i].infos, (long)stream_infos[i].infos_end - (long)stream_infos[i].infos);
    }
    free(stream_infos);
    avformat_close_input(&format_context);
}


/**
 * Read all packets from file and extract relevant infos to cache them
 */
void MediaFile::build_cache()
{
    // allocate minimalistic cache for all streams
    stream_infos = (stream_info_t*) malloc(sizeof(stream_info_t) * format_context->nb_streams);
    for (int i = 0; i < format_context->nb_streams; i++) {
        stream_infos[i].infos = (packet_info_t*) mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        stream_infos[i].num_infos = 0;
        stream_infos[i].infos_end = (packet_info_t*) ((unsigned long) stream_infos[i].infos + 4096);
    }

    // get decoder
    const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    AVCodecContext *decode_context = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decode_context, video_stream->codecpar);
    avcodec_open2(decode_context, decoder, NULL);

    // preparations
    AVPacket *packet = av_packet_alloc();

    // find all frames
    reorder_length = 0;
    int frame_count = 0;
    packet_info_t *current = stream_infos[video_stream->index].infos;
    long start_pts = LONG_MIN;
    while (av_read_frame(format_context, packet) == 0) {
        if (packet->flags & AV_PKT_FLAG_CORRUPT && frame_count) {
            printf("found corrupt packet in stream %d at pts %ld\n", packet->stream_index, packet->pts);
        }

        // logging
#ifdef TRACE
        AVStream* stream = format_context->streams[packet->stream_index];
        float timestamp = (packet->pts - start_pts) * stream->time_base.num * 1.0 / stream->time_base.den;
        std::string stream_type = "unknown";
        switch (stream->codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                stream_type = "video";
                break;
            case AVMEDIA_TYPE_AUDIO:
                stream_type = "audio";
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                stream_type = "subtitle";
                break;
            default:
                break;
        }
        printf("found %s packet of stream %d with duration %ld at %10lu with pts %ld (%.3f) and dts %ld; is key: %d; is corrupt: %d\n", stream_type.c_str(), packet->stream_index, packet->duration, packet->pos, packet->pts, timestamp, packet->dts, packet->flags & AV_PKT_FLAG_KEY, packet->flags & AV_PKT_FLAG_CORRUPT);
#endif

        // extend info area if needed
        stream_info_t* stream_info = stream_infos + packet->stream_index;
        if (stream_info->infos_end < stream_info->infos + stream_info->num_infos + 2) {
            long old_size = (unsigned long) stream_info->infos_end - (unsigned long) stream_info->infos;
            // printf("remap area for stream %d (old %#lx): %p\n", packet->stream_index, old_size, stream_info->infos);
            stream_info->infos = (packet_info_t*) mremap(stream_info->infos, old_size, old_size + 4096, MREMAP_MAYMOVE);
            // printf("remap area for stream %d (new %#lx): %p\n", packet->stream_index, old_size + 4096, stream_info->infos);
            if (stream_info->infos == MAP_FAILED) {
                perror("mremap failed");
                exit(EXIT_FAILURE);
            }
            stream_info->infos_end = (packet_info_t*) ((unsigned long) stream_info->infos + old_size + 4096);
        }

        packet_info_t* destination = stream_info->infos + stream_info->num_infos;
        if (packet->stream_index == video_stream->index) {
            // skip to first key frame and ignore frames with a pts before first keyframe
            if ((frame_count == 0 && !(packet->flags & AV_PKT_FLAG_KEY)) || packet->pts < start_pts) {
                continue;
            } else if (frame_count == 0) {
                start_pts = packet->pts;
            }

            int bframes = 1;
            while (destination > stream_info->infos && (destination-1)->pts > packet->pts) {
                if ((destination-1)->frame_type != AV_PICTURE_TYPE_I && (destination-1)->frame_type != AV_PICTURE_TYPE_P) {
                    bframes++;
                }
                memcpy(destination, destination-1, sizeof(*destination));
                destination--;
            }
            frame_count++;

            // get frame type
            AVCodecParserContext *parser_context = av_stream_get_parser(video_stream);
            if (parser_context) {
                destination->frame_type = (AVPictureType) parser_context->pict_type;
            } else {
                printf("parser context was null\n");
                // this can produce an endless loop, duplicate frames, ... as it changes the file pointer
                //AVFrame *frame = get_frame(frame_count-1);
                //if (frame) {
                //    destination->frame_type = frame->pict_type;
                //} else {
                    destination->frame_type = AV_PICTURE_TYPE_NONE;
                //}
            }
            if (destination->frame_type != AV_PICTURE_TYPE_I && destination->frame_type != AV_PICTURE_TYPE_P && bframes > reorder_length) {
                reorder_length = bframes;
            }
            destination->offset = packet->pos;
        } else {
            int64_t pos = packet->pos;
            for (packet_info_t* current = destination - 1; pos == -1 && current >= stream_info->infos; current--) {
                pos = current->offset;
            }
            if (pos == -1) {
                pos = 0;
            }
            destination->offset = pos;
            destination->frame_type = AV_PICTURE_TYPE_NONE;
        }
        destination->pts = packet->pts;
        destination->dts = packet->dts;
        destination->duration = packet->duration;
        destination->is_keyframe = packet->flags & AV_PKT_FLAG_KEY;
        destination->is_corrupt  = packet->flags & AV_PKT_FLAG_CORRUPT;
        stream_info->num_infos++;

        av_packet_unref(packet);
    }

    // fix last packets
    for (int i = frame_count-1; i >= 0 && !stream_infos[video_stream->index].infos[i].is_keyframe; i--) {
        current = stream_infos[video_stream->index].infos + i;
        if (current->frame_type == AV_PICTURE_TYPE_NONE) {
            AVFrame* frame = get_frame(i);
            if (frame) {
                current->frame_type = frame->pict_type;
                av_frame_free(&frame);
            }
        }
    }

    current = stream_infos[video_stream->index].infos;
    max_bframes = 0;
    gop_size = 0;
    max_difference = 0;
    int bframe_count = 0;
    int gop_count = 0;
    int64_t next_pts = current->pts;
    for (unsigned long i = 0; i < frame_count; next_pts = current->pts + current->duration, i++, current++) {
        // float timestamp = (current->pts - start_pts) * video_stream->time_base.num * 1.0 / video_stream->time_base.den;
        // printf("found frame %lu at %lu with pts %ld (%.3f) and dts %ld; is key: %d; is corrupt: %d; frame type: %c/%d\n", i, current->offset, current->pts, timestamp, current->dts, current->is_keyframe, current->is_corrupt, av_get_picture_type_char(current->frame_type), current->frame_type);

        if (current->pts - current->dts > max_difference) {
            max_difference = current->pts - current->dts;
        }

        if (next_pts != current->pts) {
            printf("found pts gap: expected pts %ld while current has pts %ld\n", next_pts, current->pts);
            bframe_count = 0;
            gop_count = 0;
            continue;
        }

        // track bframes
        if (current->frame_type == AV_PICTURE_TYPE_B) {
            bframe_count++;
        } else {
            if (bframe_count > max_bframes) {
                max_bframes = bframe_count;
            }
            bframe_count = 0;
        }

        // track gop size
        gop_count++;
        if (current->frame_type == AV_PICTURE_TYPE_I) {
            if (gop_count > gop_size) {
                gop_size = gop_count;
            }
            gop_count = 0;
        }
    }

    for (int i = 0; i < format_context->nb_streams; i++) {
        // only examine audio for now, since we use only one video stream and subtitles are not continous
        if (!is_audio_stream(i)) {
            continue;
        }
        current = stream_infos[i].infos;
        int64_t next_pts = current->pts;
        for (unsigned long j = 0; j < stream_infos[i].num_infos; next_pts = current->pts + current->duration, j++, current++) {
            if (next_pts != current->pts) {
                printf("found pts gap for stream %d: expected pts %ld while current has pts %ld\n", i, next_pts, current->pts);
                bframe_count = 0;
                gop_count = 0;
                continue;
            }
        }
    }

    av_packet_free(&packet);
}

/**
 * Extract a frame by index
 * @param frame_index  The frame index to extract
 * @return The extracted frame or NULL on failure
 */
AVFrame* MediaFile::get_frame(ssize_t frame_index)
{
    // get decoder
    const AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context, video_stream->codecpar);
    avcodec_open2(codec_context, codec, NULL);

    // find keyframe
    int current = find_iframe_before(frame_index);
    // printf("starting decoding at frame %d\n", current);


    stream_info_t* stream_info = stream_infos + video_stream->index;

    // get some infos
    int64_t target_pts = stream_info->infos[frame_index].pts;
    int64_t target_dts = stream_info->infos[frame_index].dts;
    int64_t start_pts  = stream_info->infos[current].pts;
    // puts("calling pframe after");
    ssize_t pframe_after = find_pframe_after(frame_index);
    if (pframe_after == -1) {
        pframe_after = find_pframe_before(frame_index);
    }
    int64_t pframe_dts = stream_info->infos[pframe_after].dts;
    // printf("start  pts: %ld\n", start_pts);
    // printf("target pts: %ld\n", target_pts);

    // set reorder buffer length
    // this is needed, frames that cause an automatic resizing are lost (at least for h264)
    codec_context->has_b_frames = max_bframes;
    // printf("has bframes: %d\n", codec_context->has_b_frames);

    // preparations
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // get frame
    int64_t offset = stream_info->infos[current].offset;
    if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
        puts("Seek failed");
        return frame;
    }

    // decode frame
    frame->pts = target_pts-1;
    while(frame->pts < target_pts) {
        if (av_read_frame(format_context, packet)) {
            puts("failed to read packet");
            av_packet_unref(packet);
            break;
        }
        if (packet->stream_index == video_stream->index && packet->pts >= start_pts) {
            // printf("found packet with dts/pts %ld/%ld\n", packet->dts, packet->pts);
            AVCodecParserContext* parser = av_stream_get_parser(video_stream);
            if (parser && (packet->dts < pframe_dts || pframe_dts == target_dts) && parser->pict_type != AV_PICTURE_TYPE_I && parser->pict_type != AV_PICTURE_TYPE_P) {
                // printf("Skipped %c frame\n", av_get_picture_type_char((AVPictureType) parser->pict_type));
            } else {
                avcodec_send_packet(codec_context, packet);
                while (frame->pts != target_pts && avcodec_receive_frame(codec_context, frame) == 0) {
                    // printf("got frame with pts %ld and type %c\n", frame->pts, av_get_picture_type_char(frame->pict_type));
                }
            }
        }
        av_packet_unref(packet);
    }

    // flush decoder
    avcodec_send_packet(codec_context, NULL);
    while (frame->pts != target_pts && avcodec_receive_frame(codec_context, frame) == 0) {
        // printf("got frame with pts %ld and type %c\n", frame->pts, av_get_picture_type_char(frame->pict_type));
    }

    // cleanup
    if (frame->pts != target_pts) {
        av_frame_free(&frame);
    }
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    return frame;
}

/**
 * Find first I frame before or at specified frame
 * @param search The index to search from
 * @return index of found I frame
 */
ssize_t MediaFile::find_iframe_before(ssize_t search) const
{
    stream_info_t* stream_info = stream_infos + video_stream->index;
    ssize_t iframe_before = search;
    if (iframe_before >= stream_info->num_infos) {
        iframe_before = stream_info->num_infos - 1;
    }
    while (iframe_before >= 0 && !stream_info->infos[iframe_before].is_keyframe) {
        iframe_before--;
    }
    return iframe_before;
}

/**
 * Find first P or I frame before or at specified frame
 * @param search The index to search from
 * @return index of found P or I frame
 */
ssize_t MediaFile::find_pframe_before(ssize_t search) const
{
    stream_info_t* stream_info = stream_infos + video_stream->index;
    ssize_t pframe_before = search;
    if (pframe_before >= stream_info->num_infos) {
        pframe_before = stream_info->num_infos - 1;
    }
    while (pframe_before >= 0 && !stream_info->infos[pframe_before].is_keyframe && stream_info->infos[pframe_before].frame_type != AV_PICTURE_TYPE_P) {
        pframe_before--;
    }
    return pframe_before;
}

/**
 * Find first I frame after or at specified frame
 * @param search The index to search from
 * @return index of found I frame
 */
ssize_t MediaFile::find_iframe_after(ssize_t search) const
{
    stream_info_t* stream_info = stream_infos + video_stream->index;
    ssize_t iframe_after = search;
    while (iframe_after < stream_info->num_infos && !stream_info->infos[iframe_after].is_keyframe) {
        iframe_after++;
    }
    return iframe_after >= stream_info->num_infos ? -1 : iframe_after;
}

/**
 * Find first P or I frame after or at specified frame
 * @param search The index to search from
 * @return index of found P or I frame
 */
ssize_t MediaFile::find_pframe_after(ssize_t search) const
{
    stream_info_t* stream_info = stream_infos + video_stream->index;
    ssize_t pframe_after = search;
    while (pframe_after < stream_info->num_infos && !stream_info->infos[pframe_after].is_keyframe && stream_info->infos[pframe_after].frame_type != AV_PICTURE_TYPE_P) {
        pframe_after++;
    }
    // printf("pframe after: %zd\n", pframe_after);
    return pframe_after >= stream_info->num_infos ? -1 : pframe_after;
}

/**
 * Get the info for the given frame or NULL if index invalid
 * @param frame_index The index of the frame
 * @return The info for the frame or NULL if index is invalid
 */
const packet_info_t * MediaFile::get_frame_info(ssize_t frame_index) const
{
    if (frame_index < 0 || frame_index >= stream_infos[video_stream->index].num_infos) {
        return NULL;
    } else {
        return stream_infos[video_stream->index].infos + frame_index;
    }
}

bool compare_packet(const packet_info_t &a, const packet_info_t &b) { return a.pts < b.pts; }

/**
 * Get the first packet ending after the given pts
 * @param stream_index The index of the stream
 * @param pts The pts to search for
 * @returns The frame info or NULL if the pts is after file end
 */
const packet_info_t *MediaFile::get_packet_info(int stream_index, int64_t pts) const
{
    if (stream_index < 0 || stream_index >= format_context->nb_streams) {
        return NULL;
    }

    // get offset for stream
    packet_info_t search = { .pts=pts, .duration=0 };
    packet_info_t * last = stream_infos[stream_index].infos + stream_infos[stream_index].num_infos;
    packet_info_t * lower_bound = std::lower_bound(stream_infos[stream_index].infos, last, search, compare_packet);
    return lower_bound == last ? NULL : lower_bound;
}

/**
 * Get the file offset of the first packet ending after the given pts
 * @param int64_t pts The pts to search for
 * @returns The offset or -1 on error
 */
ssize_t MediaFile::offset_before_pts(int64_t pts) const {
    if (stream_infos == NULL) {
        return -1;
    }

    stream_info_t* video_info = stream_infos + video_stream->index;
    const packet_info_t* lower_bound = get_packet_info(video_stream->index, pts);
    if (lower_bound == NULL) {
        lower_bound = video_info->infos + video_info->num_infos - 1;
    }
    ssize_t result = lower_bound->offset;

    // find previous I frame
    for (const packet_info_t * current = lower_bound; current >= video_info->infos && !current->is_keyframe; current--) {
        if (current->offset < result) {
            result = current->offset;
        }
    }

    // check other streams
    for (int i = 0; i < format_context->nb_streams; i++) {
        if (i == video_stream->index) {
            continue;
        }
        lower_bound = get_packet_info(i, pts);
        if (lower_bound == NULL) {
            continue;
        } else if (lower_bound->offset < result) {
            result = lower_bound->offset;
        }
    }
    return result;
}

/**
 * Get the file offset of the last packet starting before the given pts
 * @param int64_t pts The pts to search for
 * @returns The offset or file length if the pts is after the file end
 */
ssize_t MediaFile::offset_after_pts(int64_t pts) const {
    // get offset for video stream
    stream_info_t* video_info = stream_infos + video_stream->index;
    const packet_info_t* upper_bound = get_packet_info(video_stream->index, pts);
    if (upper_bound == NULL) {
        return filesize;
    }

    // find next keyframe
    // since the frames are not ordered by pts, search for the next but one keyframe
    bool keyframe_found = false;
    for (; upper_bound < video_info->infos + video_info->num_infos; upper_bound++) {
        if (upper_bound->is_keyframe) {
            if (keyframe_found) {
                break;
            }
            keyframe_found = true;
        }
    }
    if (upper_bound >= video_info->infos + video_info->num_infos) {
        return filesize;
    }
    ssize_t result = upper_bound->offset;

    // check other streams
    for (int i = 0; i < format_context->nb_streams; i++) {
        if (i == video_stream->index) {
            continue;
        }
        upper_bound = get_packet_info(i, pts);
        if (upper_bound == NULL) {
            continue;
        } else if (upper_bound->offset > result) {
            result = upper_bound->offset;
        }
    }
    return result;
}

/**
 * Check if the stream with the given index is an audio stream
 * @param stream_index The index of the stream to check
 * @returns True, if the stream exists and is an audio stream, False otherwise
 */
bool MediaFile::is_audio_stream(int stream_index) const {
    if (stream_index < -1 || stream_index >= format_context->nb_streams) {
        return false;
    }

    return format_context->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
}

