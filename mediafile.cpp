#include "mediafile.h"

#include <stdio.h>
#include <sys/mman.h>

MediaFile::MediaFile(std::string filename) : filename(filename)
{
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

        // specific for video and audio
        if (local_codec_parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf("Video Codec: resolution %d x %d\n", local_codec_parameters->width, local_codec_parameters->height);
            video_stream = stream;
        } else if (local_codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("Audio Codec: %d channels, sample rate %d\n", local_codec_parameters->ch_layout.nb_channels, local_codec_parameters->sample_rate);
            if (!audio_stream) {
                audio_stream = stream;
            } else if (!audio_stream2) {
                audio_stream2 = stream;
            }
        } else if (local_codec_parameters->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            printf("Subtitle Codec: %s\n", avcodec_get_name(local_codec_parameters->codec_id));
            if (local_codec_parameters->codec_id == AV_CODEC_ID_DVB_SUBTITLE) {
                subtitle_stream = stream;
            }
        }
        // general
        printf("\tCodec %s ID %d bit_rate %ld\n", local_codec->long_name, local_codec->id, local_codec_parameters->bit_rate);
        printf("\tDuration %ld us; timebase: %d/%d\n", stream->duration,  stream->time_base.num, stream->time_base.den);
    }

    // fill cache
    cache_frame_infos();
}

MediaFile::~MediaFile()
{
    munmap(frame_infos, sizeof(frame_info_t) * frame_count);
    avformat_free_context(format_context);
}


/**
 * Read all packets from file and extract relevant infos to cache them
 */
void MediaFile::cache_frame_infos()
{
    // allocate cache
    ssize_t nb_frames = video_stream->nb_frames;
    printf("nb_frames: %zd, duration: %ld, time_base.num: %d, time_base.den: %d, avg_frame_rate.num: %d, avg_frame_rate.den: %d\n", nb_frames, video_stream->duration, video_stream->time_base.num, video_stream->time_base.den, video_stream->avg_frame_rate.num, video_stream->avg_frame_rate.den);
    if (nb_frames == 0) {
        nb_frames = video_stream->duration * video_stream->avg_frame_rate.num / video_stream->avg_frame_rate.den * video_stream->time_base.num / video_stream->time_base.den;
    }
    printf("allocating space for %zd frames\n", nb_frames);
    frame_infos = (frame_info_t *) mmap(NULL, sizeof(frame_info_t)* nb_frames, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    // get decoder
    const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    AVCodecContext *decode_context = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decode_context, video_stream->codecpar);
    avcodec_open2(decode_context, decoder, NULL);

    // preparations
    AVPacket *packet = av_packet_alloc();

    // find all frames
    reorder_length = 0;
    frame_count = 0;
    frame_info_t *current = frame_infos;
    long start_pts = LONG_MIN;
    while (av_read_frame(format_context, packet) == 0) {
        if (packet->flags & AV_PKT_FLAG_CORRUPT && frame_count) {
            printf("found corrupt packet in stream %d at pts %ld\n", packet->stream_index, packet->pts);
        }
        if (packet->stream_index == video_stream->index) {
            // printf("found video packet with duration %ld at %lu with pts %ld and dts %ld; is key: %d; is corrupt: %d\n", packet->duration, packet->pos, packet->pts, packet->dts, packet->flags & AV_PKT_FLAG_KEY, packet->flags & AV_PKT_FLAG_CORRUPT);
            // skip to first key frame and ignore frames with a pts before first keyframe
            if ((frame_count == 0 && !(packet->flags & AV_PKT_FLAG_KEY)) || packet->pts < start_pts) {
                continue;
            } else if (frame_count == 0) {
                start_pts = packet->pts;
            }

            int bframes = 1;
            frame_info_t *destination = current;
            while (destination > frame_infos && (destination-1)->pts > packet->pts) {
                if ((destination-1)->frame_type != AV_PICTURE_TYPE_I && (destination-1)->frame_type != AV_PICTURE_TYPE_P) {
                    bframes++;
                }
                memcpy(destination, destination-1, sizeof(*destination));
                destination--;
            }
            destination->offset = packet->pos;
            destination->pts = packet->pts;
            destination->dts = packet->dts;
            destination->is_keyframe = packet->flags & AV_PKT_FLAG_KEY;
            destination->is_corrupt  = packet->flags & AV_PKT_FLAG_CORRUPT;

            current++;
            frame_count++;

            // get frame type
            AVCodecParserContext *parser_context = av_stream_get_parser(video_stream);
            if (parser_context) {
                destination->frame_type = (AVPictureType) parser_context->pict_type;
            } else {
                printf("parser context was null\n");
                AVFrame *frame = get_frame(frame_count-1);
                if (frame) {
                    destination->frame_type = frame->pict_type;
                } else {
                    destination->frame_type = AV_PICTURE_TYPE_NONE;
                }
            }
            if (destination->frame_type != AV_PICTURE_TYPE_I && destination->frame_type != AV_PICTURE_TYPE_P && bframes > reorder_length) {
                reorder_length = bframes;
            }
        } else if (audio_stream && packet->stream_index == audio_stream->index) {
            float timestamp = (packet->pts - start_pts) * audio_stream->time_base.num * 1.0 / audio_stream->time_base.den;
            // printf("found audio packet with duration %ld at %lu with pts %ld (%.3f) and dts %ld; is key: %d; is corrupt: %d\n", packet->duration, packet->pos, packet->pts, timestamp, packet->dts, packet->flags & AV_PKT_FLAG_KEY, packet->flags & AV_PKT_FLAG_CORRUPT);
        } else if (audio_stream2 && packet->stream_index == audio_stream2->index) {
            float timestamp = (packet->pts - start_pts) * audio_stream2->time_base.num * 1.0 / audio_stream2->time_base.den;
            // printf("found audio2 packet with duration %ld at %lu with pts %ld (%.3f) and dts %ld; is key: %d; is corrupt: %d\n", packet->duration, packet->pos, packet->pts, timestamp, packet->dts, packet->flags & AV_PKT_FLAG_KEY, packet->flags & AV_PKT_FLAG_CORRUPT);
        } else if (subtitle_stream && packet->stream_index == subtitle_stream->index) {
            float timestamp = (packet->pts - start_pts) * subtitle_stream->time_base.num * 1.0 / subtitle_stream->time_base.den;
            // printf("found subtitle packet with duration %ld at %lu with pts %ld (%.3f) and dts %ld; is key: %d; is corrupt: %d\n", packet->duration, packet->pos, packet->pts, timestamp, packet->dts, packet->flags & AV_PKT_FLAG_KEY, packet->flags & AV_PKT_FLAG_CORRUPT);
        }
        av_packet_unref(packet);
    }

    current = frame_infos;
    max_bframes = 0;
    int bframe_count = 0;
    int64_t duration = frame_infos[1].pts - frame_infos[0].pts;
    int64_t last_pts = current->pts;
    for (unsigned long i = 0; i < frame_count; i++, current++) {
        float timestamp = (current->pts - start_pts) * video_stream->time_base.num * 1.0 / video_stream->time_base.den;
        // printf("found frame %lu at %lu with pts %ld (%.3f) and dts %ld; is key: %d; is corrupt: %d; frame type: %c/%d\n", i, current->offset, current->pts, timestamp, current->dts, current->is_keyframe, current->is_corrupt, av_get_picture_type_char(current->frame_type), current->frame_type);

        if (last_pts + duration != current->pts) {
            printf("found pts gap: last frame had pts %ld while current has pts %ld\n", last_pts, current->pts);
            last_pts = current->pts;
            bframe_count = 0;
            continue;
        }

        if (current->frame_type == AV_PICTURE_TYPE_B) {
            bframe_count++;
        } else {
            if (bframe_count > max_bframes) {
                max_bframes = bframe_count;
            }
            bframe_count = 0;
        }
        last_pts = current->pts;
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

    // get some infos
    int64_t target_pts = frame_infos[frame_index].pts;
    int64_t target_dts = frame_infos[frame_index].dts;
    int64_t start_pts  = frame_infos[current].pts;
    // puts("calling pframe after");
    ssize_t pframe_after = find_pframe_after(frame_index);
    if (pframe_after == -1) {
        pframe_after = find_pframe_before(frame_index);
    }
    int64_t pframe_dts = frame_infos[pframe_after].dts;
    // printf("start  pts: %ld\n", start_pts);
    // printf("target pts: %ld\n", target_pts);

    // calculate reorder buffer length
    // this is needed, frames that cause an automatic resizing are lost (at least for h264)
    int reorder_length = 0;
    for (int i = frame_index; i < frame_count && frame_infos[i].frame_type != AV_PICTURE_TYPE_I && frame_infos[i].frame_type != AV_PICTURE_TYPE_P; i++) {
        if (frame_infos[i].dts <= target_dts) {
            reorder_length++;
        }
    }
    codec_context->has_b_frames = reorder_length;
    // printf("has bframes: %d\n", codec_context->has_b_frames);

    // preparations
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // get frame
    int64_t offset = frame_infos[current].offset;
    if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
        puts("Seek failed");
        return frame;
    }

    // decode frame
    frame->pts = target_pts-1;
    while(frame->pts != target_pts) {
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
ssize_t MediaFile::find_iframe_before(ssize_t search)
{
    ssize_t iframe_before = search;
    while (iframe_before >= 0 && !frame_infos[iframe_before].is_keyframe) {
        iframe_before--;
    }
    return iframe_before;
}

/**
 * Find first P or I frame before or at specified frame
 * @param search The index to search from
 * @return index of found P or I frame
 */
ssize_t MediaFile::find_pframe_before(ssize_t search)
{
    ssize_t pframe_before = search;
    while (pframe_before >= 0 && !frame_infos[pframe_before].is_keyframe && frame_infos[pframe_before].frame_type != AV_PICTURE_TYPE_P) {
        pframe_before--;
    }
    return pframe_before;
}

/**
 * Find first I frame after or at specified frame
 * @param search The index to search from
 * @return index of found I frame
 */
ssize_t MediaFile::find_iframe_after(ssize_t search)
{
    ssize_t iframe_after = search;
    while (iframe_after < frame_count && !frame_infos[iframe_after].is_keyframe) {
        iframe_after++;
    }
    return iframe_after >= frame_count ? -1 : iframe_after;
}

/**
 * Find first P or I frame after or at specified frame
 * @param search The index to search from
 * @return index of found P or I frame
 */
ssize_t MediaFile::find_pframe_after(ssize_t search)
{
    ssize_t pframe_after = search;
    while (pframe_after < frame_count && !frame_infos[pframe_after].is_keyframe && frame_infos[pframe_after].frame_type != AV_PICTURE_TYPE_P) {
        pframe_after++;
    }
    // printf("pframe after: %zd\n", pframe_after);
    return pframe_after >= frame_count ? -1 : pframe_after;
}

/**
 * Get the info for the given frame or NULL if index invalid
 * @param frame_index The index of the frame
 * @return The info for the frame or NULL if index is invalid
 */
frame_info_t* MediaFile::get_frame_info(ssize_t frame_index) {
    if (frame_index < 0 || frame_index >= frame_count) {
        return NULL;
    } else {
        return frame_infos + frame_index;
    }
}

