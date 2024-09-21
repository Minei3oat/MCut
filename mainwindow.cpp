#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <QFileDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_actionOpen_Video_triggered()
{
    // select file
    std::string filename = QFileDialog::getOpenFileName(this, "Open Video").toStdString();
    if (filename.empty()) {
        return;
    }

    // cleanup
    if (format_context) {
        avformat_free_context(format_context);
    }

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

    cache_frame_infos();

    current_frame = 0;
    render_frame();

    cut_in = -1;
    cut_out = -1;
}

void MainWindow::on_prev_frame_clicked()
{
    if (current_frame > 0) {
        current_frame--;
        render_frame();
    }
}


void MainWindow::on_next_frame_clicked()
{
    if (current_frame < frame_count-1) {
        current_frame++;
        render_frame();
    }
}


void MainWindow::on_prev_frame_2_clicked()
{
    current_frame -= 24;
    if (current_frame < 0) {
        current_frame = 0;
    }
    render_frame();
}


void MainWindow::on_next_frame_2_clicked()
{
    current_frame += 24;
    if (current_frame >= frame_count) {
        current_frame = frame_count - 1;
    }
    render_frame();
}

/**
 * Extract a frame from the stream
 * @param stream       The stream to extract the frame from
 * @param frame_index  The frame index to extract
 * @return The extracted frame or NULL on failure
 */
AVFrame* MainWindow::get_frame(AVStream *stream, ssize_t frame_index)
{
    // get decoder
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context, stream->codecpar);
    avcodec_open2(codec_context, codec, NULL);

    // find keyframe
    int current = find_iframe_before(frame_infos, frame_index);
    printf("starting decoding at frame %d\n", current);

    // get some infos
    int64_t target_pts = frame_infos[frame_index].pts;
    int64_t target_dts = frame_infos[frame_index].dts;
    int64_t start_pts  = frame_infos[current].pts;
    puts("calling pframe after");
    ssize_t pframe_after = find_pframe_after(frame_infos, frame_index, frame_count);
    if (pframe_after == -1) {
        pframe_after = find_pframe_before(frame_infos, frame_index);
    }
    int64_t pframe_dts = frame_infos[pframe_after].dts;
    printf("start  pts: %ld\n", start_pts);
    printf("target pts: %ld\n", target_pts);

    // calculate reorder buffer length
    // this is needed, frames that cause an automatic resizing are lost (at least for h264)
    int reorder_length = 0;
    for (int i = frame_index; i < frame_count && frame_infos[i].frame_type != AV_PICTURE_TYPE_I && frame_infos[i].frame_type != AV_PICTURE_TYPE_P; i++) {
        if (frame_infos[i].dts <= target_dts) {
            reorder_length++;
        }
    }
    codec_context->has_b_frames = reorder_length;
    printf("has bframes: %d\n", codec_context->has_b_frames);

    // preparations
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // get frame
    int64_t offset = frame_infos[current].offset;
    if (avformat_seek_file(format_context, stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
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
        if (packet->stream_index == stream->index && packet->pts >= start_pts) {
            printf("found packet with dts/pts %ld/%ld\n", packet->dts, packet->pts);
            AVCodecParserContext* parser = av_stream_get_parser(video_stream);
            if (parser && (packet->dts < pframe_dts || pframe_dts == target_dts) && parser->pict_type != AV_PICTURE_TYPE_I && parser->pict_type != AV_PICTURE_TYPE_P) {
                printf("Skipped %c frame\n", av_get_picture_type_char((AVPictureType) parser->pict_type));
            } else {
                avcodec_send_packet(codec_context, packet);
                while (frame->pts != target_pts && avcodec_receive_frame(codec_context, frame) == 0) {
                    printf("got frame with pts %ld and type %c\n", frame->pts, av_get_picture_type_char(frame->pict_type));
                }
            }
        }
        av_packet_unref(packet);
    }

    // flush decoder
    avcodec_send_packet(codec_context, NULL);
    while (frame->pts != target_pts && avcodec_receive_frame(codec_context, frame) == 0) {
        printf("got frame with pts %ld and type %c\n", frame->pts, av_get_picture_type_char(frame->pict_type));
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
 * Render the currently selected frame
 */
void MainWindow::render_frame()
{
    struct timeval start, end;
    gettimeofday(&start, NULL);

    if (!video_stream || current_frame >= frame_count) {
        return;
    }

    // get frame
    AVFrame* frame = get_frame(video_stream, current_frame);
    if (!frame) {
        puts("frame not found");
        return;
    }

    // convert frame to RGB
    AVFrame *rgb = av_frame_alloc();
    rgb->width = frame->width;
    rgb->height = frame->height;
    rgb->format = AV_PIX_FMT_RGB24;
    av_frame_get_buffer(rgb, 4);
    struct SwsContext *sws_context = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, rgb->width, rgb->height, (AVPixelFormat)rgb->format, SWS_BILINEAR, NULL, NULL, NULL);
    sws_scale_frame(sws_context, rgb, frame);

    // render frame
    QImage image(rgb->data[0], rgb->width, rgb->height, QImage::Format_RGB888);
    QPixmap pm(QPixmap::fromImage(image));
    ui->video_frame->setPixmap(pm);
    ui->video_frame->setScaledContents(true);
    ui->video_frame->update();
    // printf("rendered frame %lu\n", current_frame);

    // update current position
    char buffer[64] = "";
    sprintf(buffer, "%lu [%c/%c]", current_frame, av_get_picture_type_char(frame->pict_type), av_get_picture_type_char(frame_infos[current_frame].frame_type));
    // printf("pts: [%ld/%ld]\n", frame->pts, frame_infos[current_frame].pts);
    // printf("dts: [%ld/%ld]\n", frame->pkt_dts, frame_infos[current_frame].dts);
    ui->current_pos->setText(QString(buffer));

    // cleanup
    sws_freeContext(sws_context);
    av_frame_free(&frame);
    av_frame_free(&rgb);

    gettimeofday(&end, NULL);
    // printf("rendered frame in %lu us\n", end.tv_usec-start.tv_usec);
}

/**
 * Read all packets from file and extract relevant infos to cache them
 */
void MainWindow::cache_frame_infos()
{
    if (!frame_infos) {
        frame_infos = (frame_info_t *) mmap(NULL, 1024*4096*sizeof(frame_info_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
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
    frame_count = 0;
    frame_info_t *current = frame_infos;
    long start_pts = LONG_MIN;
    while (av_read_frame(format_context, packet) == 0) {
        if (packet->stream_index == video_stream->index) {
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

            // get frame type
            AVCodecParserContext *parser_context = av_stream_get_parser(video_stream);
            if (parser_context) {
                destination->frame_type = (AVPictureType) parser_context->pict_type;
            } else {
                printf("parser context was null\n");
                AVFrame *frame = get_frame(video_stream, frame_count);
                if (frame) {
                    destination->frame_type = frame->pict_type;
                } else {
                    destination->frame_type = AV_PICTURE_TYPE_NONE;
                }
            }
            if (destination->frame_type != AV_PICTURE_TYPE_I && destination->frame_type != AV_PICTURE_TYPE_P && bframes > reorder_length) {
                reorder_length = bframes;
            }

            current++;
            frame_count++;
        } else if (audio_stream && packet->stream_index == audio_stream->index) {
            float timestamp = (packet->pts - start_pts) * audio_stream->time_base.num * 1.0 / audio_stream->time_base.den;
            printf("found audio packet with duration %ld at %lu with pts %ld (%.3f) and dts %ld; is key: %d; is corrupt: %d\n", packet->duration, packet->pos, packet->pts, timestamp, packet->dts, packet->flags & AV_PKT_FLAG_KEY, packet->flags & AV_PKT_FLAG_CORRUPT);
        } else if (subtitle_stream && packet->stream_index == subtitle_stream->index) {
            float timestamp = (packet->pts - start_pts) * subtitle_stream->time_base.num * 1.0 / subtitle_stream->time_base.den;
            printf("found subtitle packet with duration %ld at %lu with pts %ld (%.3f) and dts %ld; is key: %d; is corrupt: %d\n", packet->duration, packet->pos, packet->pts, timestamp, packet->dts, packet->flags & AV_PKT_FLAG_KEY, packet->flags & AV_PKT_FLAG_CORRUPT);
        }
        av_packet_unref(packet);
    }

    current = frame_infos;
    max_bframes = 0;
    int bframe_count = 0;
    int64_t duration = frame_infos[1].pts - frame_infos[0].pts;
    int64_t last_pts = current->pts;
    for (unsigned long i = 0; i < frame_count; i++, current++) {
        printf("found frame %lu at %lu with pts %ld and dts %ld; is key: %d; is corrupt: %d; frame type: %c/%d\n", i, current->offset, current->pts, current->dts, current->is_keyframe, current->is_corrupt, av_get_picture_type_char(current->frame_type), current->frame_type);

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

void MainWindow::on_set_cut_in_clicked()
{
    cut_in = current_frame;
    ui->cut_in_pos->setText(ui->current_pos->text());
}


void MainWindow::on_set_cut_out_clicked()
{
    cut_out = current_frame;
    ui->cut_out_pos->setText(ui->current_pos->text());
}


/**
 * Find first I frame before or at specified frame
 * @return index of found I frame
 */
ssize_t MainWindow::find_iframe_before(frame_info_t *frame_infos, ssize_t search)
{
    ssize_t iframe_before = search;
    while (iframe_before >= 0 && !frame_infos[iframe_before].is_keyframe) {
        iframe_before--;
    }
    return iframe_before;
}

/**
 * Find first P or I frame before or at specified frame
 * @return index of found P or I frame
 */
ssize_t MainWindow::find_pframe_before(frame_info_t *frame_infos, ssize_t search)
{
    ssize_t pframe_before = search;
    while (pframe_before >= 0 && !frame_infos[pframe_before].is_keyframe && frame_infos[pframe_before].frame_type != AV_PICTURE_TYPE_P) {
        pframe_before--;
    }
    return pframe_before;
}

/**
 * Find first I frame after or at specified frame
 * @return index of found I frame
 */
ssize_t MainWindow::find_iframe_after(frame_info_t *frame_infos, ssize_t search, ssize_t frame_count)
{
    ssize_t iframe_after = search;
    while (iframe_after < frame_count && !frame_infos[iframe_after].is_keyframe) {
        iframe_after++;
    }
    return iframe_after >= frame_count ? -1 : iframe_after;
}

/**
 * Find first P or I frame after or at specified frame
 * @return index of found P or I frame
 */
ssize_t MainWindow::find_pframe_after(frame_info_t *frame_infos, ssize_t search, ssize_t frame_count)
{
    ssize_t pframe_after = search;
    while (pframe_after < frame_count && !frame_infos[pframe_after].is_keyframe && frame_infos[pframe_after].frame_type != AV_PICTURE_TYPE_P) {
        pframe_after++;
    }
    printf("pframe after: %zd\n", pframe_after);
    return pframe_after >= frame_count ? -1 : pframe_after;
}


void MainWindow::transcode_video_frames(AVFormatContext *format_context, AVStream *video_stream, frame_info_t *frame_infos, ssize_t cut_in, ssize_t cut_out, AVFormatContext *output_context, AVStream *output_stream, int64_t start_dts) {
    ssize_t iframe_before = find_iframe_before(frame_infos, cut_in);
    ssize_t current = iframe_before;

    // get decoder
    const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    AVCodecContext *decode_context = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decode_context, video_stream->codecpar);
    avcodec_open2(decode_context, decoder, NULL);
    decode_context->has_b_frames = reorder_length;

    // get encoder
    const AVCodec *encoder = avcodec_find_encoder(output_stream->codecpar->codec_id);
    AVCodecContext *encode_context = avcodec_alloc_context3(encoder);
    avcodec_parameters_to_context(encode_context, output_stream->codecpar);
    encode_context->time_base.den = video_stream->avg_frame_rate.num;
    encode_context->time_base.num = video_stream->avg_frame_rate.den;
    encode_context->max_b_frames = max_bframes;
    encode_context->gop_size = cut_out - cut_in + 2;

    // calculate bitrate
    if (encode_context->bit_rate == 0) {
        puts("calculating bitrate");
        ssize_t offset_diff = frame_infos[frame_count-1].offset - frame_infos[0].offset;
        encode_context->bit_rate = offset_diff * 8 * video_stream->avg_frame_rate.num / video_stream->avg_frame_rate.den / frame_count;
    }
    printf("encoder: bitrate: %ld; global_quality: %d\n", encode_context->bit_rate, encode_context->global_quality);
    avcodec_open2(encode_context, encoder, NULL);

    // preparations
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // dts correction
    int64_t dts = start_dts;
    int64_t duration = frame_infos[cut_in+1].pts - frame_infos[cut_in].pts;

    // get frames
    int64_t offset = frame_infos[current].offset;
    if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
        puts("Seek failed");
        return;
    }
    while (current <= cut_out) {
        if (av_read_frame(format_context, packet)) {
            puts("failed to read packet");
            break;
        }
        if (packet->stream_index == video_stream->index) {
            avcodec_send_packet(decode_context, packet);
            while (avcodec_receive_frame(decode_context, frame) == 0) {
                // skip frames just needed for decoding
                if (current < cut_in) {
                    printf("skipped frame %zd with dts %ld and pts %ld\n", current, frame->pkt_dts, frame->pts);
                    current++;
                    continue;
                }

                // send frame to encoder
                printf("got frame %zd with dts %ld and pts %ld\n", current, frame->pkt_dts, frame->pts);
                frame->pict_type = AV_PICTURE_TYPE_NONE;
                avcodec_send_frame(encode_context, frame);

                // write encoded packets
                while (avcodec_receive_packet(encode_context, packet) == 0) {
                    packet->dts = dts;
                    dts += duration;
                    printf("Writing transcoded packet with dts %ld and pts %ld\n", packet->dts, packet->pts);
                    packet->stream_index = output_stream->index;
                    av_interleaved_write_frame(output_context, packet);
                }

                current++;
            }
        }
        av_packet_unref(packet);
    }

    // write remaining encoded packets
    avcodec_send_frame(encode_context, NULL);
    while (avcodec_receive_packet(encode_context, packet) == 0) {
        packet->dts = dts;
        dts += duration;
        printf("Writing transcoded packet with dts %ld and pts %ld\n", packet->dts, packet->pts);
        packet->stream_index = output_stream->index;
        av_interleaved_write_frame(output_context, packet);
    }

    // cleanup
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decode_context);
    avcodec_free_context(&encode_context);
}


void MainWindow::on_actionCut_Video_triggered()
{
    if (!video_stream) {
        puts("Video stream missing");
        return;
    } else if (cut_in < 0 || cut_out < 0) {
        puts("CutIn or CutOut missing");
        return;
    } else if (cut_in > cut_out) {
        puts("CutIn needs to be before CutOut");
        return;
    }

    // select file
    std::string filename = QFileDialog::getSaveFileName(this, "Open Video").toStdString();
    if (filename.empty()) {
        return;
    }

    // open output file
    AVFormatContext *output_context;
    avformat_alloc_output_context2(&output_context, NULL, NULL, filename.c_str());

    // add streams
    AVStream *output_video_stream = avformat_new_stream(output_context, NULL);
    avcodec_parameters_copy(output_video_stream->codecpar, video_stream->codecpar);
    output_video_stream->avg_frame_rate = video_stream->avg_frame_rate;
    AVStream *output_audio_stream = avformat_new_stream(output_context, NULL);
    avcodec_parameters_copy(output_audio_stream->codecpar, audio_stream->codecpar);
    AVStream *output_subtitle_stream = avformat_new_stream(output_context, NULL);
    avcodec_parameters_copy(output_subtitle_stream->codecpar, subtitle_stream->codecpar);

    // create index translation table
    int *index = (int *) malloc(sizeof(int) * format_context->nb_streams);
    memset(index, 0xff, sizeof(int) * format_context->nb_streams);
    index[video_stream->index] = output_video_stream->index;
    index[audio_stream->index] = output_audio_stream->index;
    index[subtitle_stream->index] = output_subtitle_stream->index;

    // write header
    avio_open(&output_context->pb, filename.c_str(), AVIO_FLAG_WRITE);
    if (avformat_write_header(output_context, NULL) < 0) {
        puts("Failed writing header");
    }

    ssize_t remux_start = find_iframe_after(frame_infos, cut_in, frame_count);
    ssize_t remux_end = find_pframe_before(frame_infos, cut_out);
    ssize_t remux_save_end = find_iframe_after(frame_infos, remux_end+1, frame_count);
    printf("computed values: %ld/%ld/%ld\n", remux_start, remux_end, remux_save_end);

    if (cut_in < remux_start) {
        // transcode frames before first i-frame
        int64_t start_dts = frame_infos[remux_start].dts - (frame_infos[remux_start].pts - frame_infos[cut_in].pts);
        transcode_video_frames(format_context, video_stream, frame_infos, cut_in, remux_start-1, output_context, output_video_stream, start_dts);
    }

    // remux frames between first i-frame and last p-frame
    AVPacket *packet = av_packet_alloc();
    int64_t packet_length_dts = frame_infos[cut_in+1].pts - frame_infos[cut_in].pts;
    long start_pts = frame_infos[remux_start].pts;
    long end_pts = frame_infos[remux_end].pts + packet_length_dts;
    ssize_t current = remux_start;
    int64_t offset = frame_infos[current].offset;
    if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
        puts("Seek failed");
        return;
    }
    int64_t last_dts = frame_infos[remux_start].dts - packet_length_dts;
    while (current < remux_save_end || last_dts < end_pts + packet_length_dts) {
        if (av_read_frame(format_context, packet)) {
            puts("failed to read packet");
            break;
        }
        if (packet->pts >= start_pts && packet->pts + packet->duration <= end_pts + packet_length_dts) {
            if (index[packet->stream_index] != -1) {
                printf("Writing packet with dts %ld and pts %ld\n", packet->dts, packet->pts);
                last_dts = packet->dts;
                packet->stream_index = index[packet->stream_index];
                av_interleaved_write_frame(output_context, packet);
            }
        }
        if (packet->stream_index == video_stream->index) {
            current++;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    printf("original - frame rate: %d/%d; time_base: %d/%d\n", video_stream->avg_frame_rate.num, video_stream->avg_frame_rate.den, video_stream->time_base.num, video_stream->time_base.den);
    printf("output   - frame rate: %d/%d; time_base: %d/%d\n", output_video_stream->avg_frame_rate.num, output_video_stream->avg_frame_rate.den, output_video_stream->time_base.num, output_video_stream->time_base.den);

    if (remux_end < cut_out) {
        // transcode frames after last p-frame
        transcode_video_frames(format_context, video_stream, frame_infos, remux_end+1, cut_out, output_context, output_video_stream, last_dts + packet_length_dts);
    }
    puts("transcoded");

    // write trailer
    av_write_trailer(output_context);

    // cleanup
    avio_closep(&output_context->pb);
    avformat_free_context(output_context);
}


void MainWindow::on_actionExit_triggered()
{
    exit(EXIT_SUCCESS);
}

