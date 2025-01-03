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
    if (num_media_files >= MAX_MEDIA_FILES) {
        puts("Maximum number of media files open!");
        return;
    }

    // select file
    std::string filename = QFileDialog::getOpenFileName(this, "Open Video").toStdString();
    if (filename.empty()) {
        return;
    }

    // load media file
    media_files[num_media_files] = new MediaFile(filename);

    // ui->position_slider->setSliderDown(false);
    ui->position_slider->setMaximum(media_files[num_media_files]->get_frame_count()-1);
    ui->position_slider->setSliderPosition(0);

    current_media_file = num_media_files;
    num_media_files++;
    ui->current_media_file->setText(QString::fromStdString(media_files[current_media_file]->get_filename()));
    render_frame();

    cut_in = -1;
    cut_out = -1;
}

void MainWindow::on_prev_media_file_clicked()
{
    if (current_media_file > 0) {
        current_media_file--;
        ui->current_media_file->setText(QString::fromStdString(media_files[current_media_file]->get_filename()));
        render_frame();
    }
}

void MainWindow::on_next_media_file_clicked()
{
    if (current_media_file < num_media_files-1) {
        current_media_file++;
        ui->current_media_file->setText(QString::fromStdString(media_files[current_media_file]->get_filename()));
        render_frame();
    }
}

void MainWindow::on_prev_frame_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    MediaFile* media_file = media_files[current_media_file];
    if (media_file->current_frame > 0) {
        media_file->current_frame--;
        render_frame();
    }
}

void MainWindow::on_next_frame_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    MediaFile* media_file = media_files[current_media_file];
    if (media_file->current_frame < media_file->get_frame_count()-1) {
        media_file->current_frame++;
        render_frame();
    }
}

void MainWindow::on_prev_frame_2_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    MediaFile* media_file = media_files[current_media_file];
    media_file->current_frame -= 48;
    if (media_file->current_frame < 0) {
        media_file->current_frame = 0;
    }
    render_frame();
}

void MainWindow::on_next_frame_2_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    MediaFile* media_file = media_files[current_media_file];
    media_file->current_frame += 48;
    if (media_file->current_frame >= media_file->get_frame_count()) {
        media_file->current_frame = media_file->get_frame_count() - 1;
    }
    render_frame();
}

void MainWindow::on_position_slider_sliderMoved(int position)
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    MediaFile* media_file = media_files[current_media_file];
    ssize_t target = media_file->find_iframe_after(position);
    if (target == -1) {
        target = media_file->find_iframe_before( position);
    }

    if (target != media_file->current_frame) {
        // printf("Sliding to frame %zd\n", target);
        media_file->current_frame = target;
        render_frame();
    }
}

void MainWindow::on_set_cut_in_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    cut_in = media_files[current_media_file]->current_frame;
    ui->cut_in_pos->setText(ui->current_pos->text());
}


void MainWindow::on_set_cut_out_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    cut_out = media_files[current_media_file]->current_frame;
    ui->cut_out_pos->setText(ui->current_pos->text());
}

void MainWindow::on_go_cut_in_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    media_files[current_media_file]->current_frame = cut_in;
    render_frame();
}

void MainWindow::on_go_cut_out_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    media_files[current_media_file]->current_frame = cut_out;
    render_frame();
}

void MainWindow::on_add_cut_clicked() {
    num_cuts = 0;
    if (current_media_file < 0 || current_media_file >= num_media_files
        || num_cuts >= MAX_CUTS || cut_in == -1 || cut_out == -1
    ) {
        return;
    }

    cuts[num_cuts].cut_in = cut_in;
    cuts[num_cuts].cut_out = cut_out;
    cuts[num_cuts].media_file = media_files[current_media_file];
    num_cuts++;
    // cut_in = -1;
    // cut_out = -1;
}

/**
 * Render the currently selected frame
 */
void MainWindow::render_frame()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    MediaFile* media_file = media_files[current_media_file];

    ui->position_slider->setSliderPosition(media_file->current_frame);

    // get frame
    AVFrame* frame = media_file->get_frame(media_file->current_frame);
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

    // update current position
    char buffer[64] = "";
    AVRational frame_rate = media_file->get_video_stream()->avg_frame_rate;
    int64_t real_time = media_file->current_frame * 1000 * frame_rate.den / frame_rate.num;
    sprintf(buffer, "%ld:%02ld:%02ld.%03ld - %lu [%c/%c] (%ld)", real_time/1000/3600, (real_time/1000/60) % 60, (real_time/1000) % 60, real_time % 1000, media_file->current_frame, av_get_picture_type_char(frame->pict_type), av_get_picture_type_char((AVPictureType) media_file->get_frame_info(media_file->current_frame)->frame_type), frame->pts);
    ui->current_pos->setText(QString(buffer));

    // cleanup
    sws_freeContext(sws_context);
    av_frame_free(&frame);
    av_frame_free(&rgb);

    gettimeofday(&end, NULL);
    // printf("rendered frame in %lu us\n", end.tv_usec-start.tv_usec);
}

/**
 * Transcode video frames
 * @param media_file The source to transcode
 * @param cut_in The first frame to transcode
 * @param cut_out The last frame to transcode
 * @param output_context The AVFormatContext of the output
 * @param output_stream The video stream of the output
 * @param start_dts The dts of the first frame
 * @param pts_offset The difference between the pts in the input and the output
 */
void MainWindow::transcode_video_frames(MediaFile* media_file, ssize_t cut_in, ssize_t cut_out, AVFormatContext* output_context, AVStream* output_stream, int64_t start_dts, int64_t pts_offset) {
    AVFormatContext* format_context = media_file->get_format_context();
    AVStream* video_stream = media_file->get_video_stream();
    const packet_info_t * frame_infos = media_file->get_frame_info(0);

    ssize_t iframe_before = media_file->find_iframe_before(cut_in);
    ssize_t current = iframe_before;

    // get decoder
    const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    AVCodecContext *decode_context = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decode_context, video_stream->codecpar);
    avcodec_open2(decode_context, decoder, NULL);
    decode_context->has_b_frames = media_file->get_reorder_length();

    // get encoder
    const AVCodec* encoder = avcodec_find_encoder(output_stream->codecpar->codec_id);
    AVCodecContext* encode_context = avcodec_alloc_context3(encoder);
    avcodec_parameters_to_context(encode_context, output_stream->codecpar);
    encode_context->time_base.den = video_stream->avg_frame_rate.num;
    encode_context->time_base.num = video_stream->avg_frame_rate.den;
    encode_context->max_b_frames = media_file->get_max_bframes();
    encode_context->gop_size = cut_out - cut_in + 2;

    // calculate bitrate
    if (encode_context->bit_rate == 0) {
        puts("calculating bitrate");
        ssize_t offset_diff = frame_infos[media_file->get_frame_count()-1].offset - frame_infos[0].offset;
        encode_context->bit_rate = offset_diff * 8 * video_stream->avg_frame_rate.num / video_stream->avg_frame_rate.den / media_file->get_frame_count();
    }
    printf("encoder: bitrate: %ld; global_quality: %d\n", encode_context->bit_rate, encode_context->global_quality);
    avcodec_open2(encode_context, encoder, NULL);

    // preparations
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    // dts correction
    int64_t dts = start_dts;
    int64_t duration = frame_infos[cut_in+1].pts - frame_infos[cut_in].pts;
    printf("packet duration: %ld\n", duration);

    // transcode video packets
    int64_t offset = frame_infos[current].offset;
    if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
        puts("Seek failed");
        return;
    }
    int64_t end_pts = frame_infos[cut_out].pts + duration;
    int64_t start_pts = frame_infos[cut_in].pts;
    printf("start_pts: %ld; end_pts = %ld\n", start_pts, end_pts);
    int64_t last_pos = 0;
    int64_t end_pos = frame_infos[cut_out].offset;
    while (last_pos <= end_pos) {
        if (av_read_frame(format_context, packet)) {
            puts("failed to read packet");
            break;
        }
        last_pos = packet->pos;

        // printf("Found packet from stream %d with dts %ld and pts %ld\n", packet->stream_index, packet->dts, packet->pts);
        if (packet->stream_index == video_stream->index) {
            avcodec_send_packet(decode_context, packet);
            while (avcodec_receive_frame(decode_context, frame) == 0) {
                // skip frames just needed for decoding
                if (frame->pts < start_pts || frame->pts >= end_pts) {
                    // printf("skipped frame %zd with dts %ld and pts %ld\n", current, frame->pkt_dts, frame->pts);
                    current++;
                    continue;
                }

                // send frame to encoder
                // printf("got frame %zd with dts %ld and pts %ld\n", current, frame->pkt_dts, frame->pts);
                frame->pict_type = AV_PICTURE_TYPE_NONE;
                frame->pts -= pts_offset;
                avcodec_send_frame(encode_context, frame);

                // retrieve encoded packets
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

    // retrieve remaining encoded packets
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
    if (!num_cuts) {
        puts("cuts missing");
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

    // get infos
    AVFormatContext* format_context = cuts[0].media_file->get_format_context();
    AVStream* video_stream = cuts[0].media_file->get_video_stream();
    const packet_info_t * frame_infos = cuts[0].media_file->get_frame_info(0);

    // prepare index translation table
    int *index = (int *) malloc(sizeof(int) * format_context->nb_streams);
    memset(index, 0xff, sizeof(int) * format_context->nb_streams);

    // add streams
    AVStream *output_video_stream = avformat_new_stream(output_context, NULL);
    avcodec_parameters_copy(output_video_stream->codecpar, video_stream->codecpar);
    output_video_stream->avg_frame_rate = video_stream->avg_frame_rate;
    output_video_stream->time_base = video_stream->time_base;
    index[video_stream->index] = output_video_stream->index;
    output_video_stream->disposition = video_stream->disposition;
    av_dict_copy(&output_video_stream->metadata, video_stream->metadata, 0);

    // track next dts
    int64_t* next_dts = (int64_t*) malloc(sizeof(int64_t) * format_context->nb_streams);

    // analyze streams
    for (int i = 0; i < format_context->nb_streams; i++)
    {
        next_dts[i] = format_context->streams[i]->start_time;
        if (i == video_stream->index) {
            continue;
        }

        AVStream* input_stream = format_context->streams[i];
        // AVCodecParameters *local_codec_parameters = input_stream->codecpar;
        // const AVCodec *local_codec = avcodec_find_decoder(local_codec_parameters->codec_id);
        // if (local_codec == NULL) {
        //     printf("No codec found\n");
        //     continue;
        // }

        // copy audio and DVB subtitles
        // if (local_codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO
        //     || (local_codec_parameters->codec_type == AVMEDIA_TYPE_SUBTITLE && local_codec_parameters->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
        // ) {
            AVStream* output_stream = avformat_new_stream(output_context, NULL);
            avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar);
            output_stream->disposition = input_stream->disposition;
            av_dict_copy(&output_stream->metadata, input_stream->metadata, 0);
            index[input_stream->index] = output_stream->index;
        // }
    }

    // set max interleave delta
    output_context->max_interleave_delta = 0;
    for (ssize_t i = 0; i < cuts[0].media_file->get_frame_count(); i++) {
        if (frame_infos[i].pts - frame_infos[i].dts > output_context->max_interleave_delta) {
            output_context->max_interleave_delta = frame_infos[i].pts - frame_infos[i].dts;
        }
    }

    printf("original - frame rate: %d/%d; time_base: %d/%d\n", video_stream->avg_frame_rate.num, video_stream->avg_frame_rate.den, video_stream->time_base.num, video_stream->time_base.den);
    printf("output   - frame rate: %d/%d; time_base: %d/%d\n", output_video_stream->avg_frame_rate.num, output_video_stream->avg_frame_rate.den, output_video_stream->time_base.num, output_video_stream->time_base.den);

    // write header
    avio_open(&output_context->pb, filename.c_str(), AVIO_FLAG_WRITE);
    if (avformat_write_header(output_context, NULL) < 0) {
        puts("Failed writing header");
        avio_closep(&output_context->pb);
        avformat_free_context(output_context);
        return;
    }

    // iterate over all cuts
    for (int i = 0; i < num_cuts; i++) {
        // get infos
        AVFormatContext* format_context = cuts[i].media_file->get_format_context();
        AVStream* video_stream = cuts[i].media_file->get_video_stream();
        const packet_info_t * frame_infos = cuts[i].media_file->get_frame_info(0);

        // get timings
        ssize_t remux_start = cuts[i].media_file->find_iframe_after(cuts[i].cut_in);
        ssize_t remux_end = cuts[i].media_file->find_pframe_before(cuts[i].cut_out);
        ssize_t remux_save_end = cuts[i].media_file->find_iframe_after(remux_end+1);
        printf("computed values: %ld/%ld/%ld\n", remux_start, remux_end, remux_save_end);

        int64_t unused_dts = 0;
        if (cuts[i].cut_in < remux_start) {
            for (int j = cuts[i].cut_in; j <= cuts[i].cut_out && !frame_infos[j].is_keyframe; j++) {
                if (frame_infos[j].dts > frame_infos[remux_start].dts) {
                    unused_dts += frame_infos[i].duration;
                }
            }
            // transcode frames before first i-frame
            printf("unused dts: %ld\n", unused_dts);
            int64_t start_dts = frame_infos[remux_start].dts - (frame_infos[remux_start].pts - frame_infos[cuts[i].cut_in].pts) + unused_dts;
            transcode_video_frames(cuts[i].media_file, cuts[i].cut_in, remux_start-1, output_context, output_video_stream, start_dts, 0);
        }

        // remux frames between first i-frame and last p-frame
        AVPacket *packet = av_packet_alloc();
        int64_t packet_length_dts = frame_infos[cuts[i].cut_in].duration;
        long start_pts = frame_infos[cuts[i].cut_in].pts;
        long end_pts = frame_infos[cuts[i].cut_out].pts + packet_length_dts;
        long remux_start_pts = frame_infos[remux_start].pts;
        long remux_end_pts = frame_infos[remux_end].pts + packet_length_dts;
        int64_t offset = cuts[i].media_file->offset_before_pts(frame_infos[cuts[i].cut_in].pts);
        if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
            puts("Seek failed");
            return;
        }
        int64_t last_offset = offset;
        int64_t last_video_dts = 0;
        int64_t loop_end = cuts[i].media_file->offset_after_pts(end_pts);
        printf("Looping from %ld to %ld\n", offset, loop_end);
        while (last_offset <= loop_end) {
            if (av_read_frame(format_context, packet)) {
                puts("failed to read packet");
                break;
            }
            last_offset = packet->pos;
            if (index[packet->stream_index] != -1 && (
                (packet->stream_index != video_stream->index && packet->pts >= start_pts && packet->pts + packet->duration <= end_pts) ||
                (packet->stream_index == video_stream->index && packet->pts >= remux_start_pts && packet->pts + packet->duration <= remux_end_pts)
            )) {
                if (packet->stream_index == video_stream->index) {
                    if (packet->pts == remux_start_pts) {
                        packet->dts += unused_dts;
                    }
                    last_video_dts = packet->dts;
                }
                // printf("Writing packet for stream %d with dts %ld and pts %ld\n", packet->stream_index, packet->dts, packet->pts);
                packet->stream_index = index[packet->stream_index];
                av_interleaved_write_frame(output_context, packet);
            }
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        printf("original - frame rate: %d/%d; time_base: %d/%d\n", video_stream->avg_frame_rate.num, video_stream->avg_frame_rate.den, video_stream->time_base.num, video_stream->time_base.den);
        printf("output   - frame rate: %d/%d; time_base: %d/%d\n", output_video_stream->avg_frame_rate.num, output_video_stream->avg_frame_rate.den, output_video_stream->time_base.num, output_video_stream->time_base.den);

        if (remux_end < cuts[i].cut_out) {
            // transcode frames after last p-frame
            transcode_video_frames(cuts[i].media_file, remux_end+1, cuts[i].cut_out, output_context, output_video_stream, last_video_dts + packet_length_dts, 0);
        }
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
