#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <QFileDialog>

// #define TRACE

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

void MainWindow::change_media_file() {
    ui->current_media_file->setText(QString::fromStdString(media_files[current_media_file]->get_filename()));
    ui->position_slider->setMaximum(media_files[current_media_file]->get_frame_count() - 1);
    render_frame();
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

    ui->position_slider->setSliderPosition(0);

    current_media_file = num_media_files;
    num_media_files++;

    change_media_file();
}

void MainWindow::on_prev_media_file_clicked()
{
    if (current_media_file <= 0) {
        return;
    }

    current_media_file--;
    change_media_file();
}

void MainWindow::on_next_media_file_clicked()
{
    if (current_media_file >= num_media_files - 1) {
        return;
    }

    current_media_file++;
    change_media_file();
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

void MainWindow::on_jump_to_frame_returnPressed()
{
    long jump = ui->jump_to_frame->text().toLong();
    if (jump < 0 || jump >= media_files[current_media_file]->get_frame_count()) {
        return;
    }
    media_files[current_media_file]->current_frame = jump;
    render_frame();
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
    media_files[current_media_file]->current_frame = cut_in;
    render_frame();
}

void MainWindow::on_go_cut_out_clicked()
{
    media_files[current_media_file]->current_frame = cut_out;
    render_frame();
}

void MainWindow::on_add_cut_clicked() {
    if (current_media_file < 0 || current_media_file >= num_media_files || num_cuts >= MAX_CUTS) {
        return;
    }

    cuts[current_cut].media_file = media_files[current_media_file];
    cuts[current_cut].cut_in = cut_in;
    cuts[current_cut].cut_out = cut_out;
    current_cut++;

    // add new cut if last was used
    if (current_cut >= num_cuts) {
        num_cuts++;
    }

    // update number of cuts and total runtime
    char buffer[64] = "";
    ssize_t total_frames = 0;
    for (int i = 0; i < num_cuts - 1; i++) {
        total_frames += cuts[i].cut_out - cuts[i].cut_in + 1;
    }
    AVRational frame_rate = media_files[0]->get_video_stream()->avg_frame_rate;
    int64_t real_time = total_frames * 1000 * frame_rate.den / frame_rate.num;
    sprintf(buffer, "%zd cuts - %ld:%02ld:%02ld.%03ld (%lu)", num_cuts - 1, real_time/1000/3600, (real_time/1000/60) % 60, (real_time/1000) % 60, real_time % 1000, total_frames);
    ui->statusbar->showMessage(QString(buffer));
}

void MainWindow::change_cut() {
    for (int i = 0; i < num_media_files; i++) {
        if (media_files[i] == cuts[current_cut].media_file) {
            current_media_file = i;
            change_media_file();
            break;
        }
    }
    cut_in = cuts[current_cut].cut_in;
    cut_out = cuts[current_cut].cut_out;

    // update resulting cut time
    char buffer[128] = "";
    ssize_t total_frames_before = 0;
    for (int i = 0; i < current_cut; i++) {
        total_frames_before += cuts[i].cut_out - cuts[i].cut_in + 1;
    }
    AVRational frame_rate = media_files[0]->get_video_stream()->avg_frame_rate;
    int64_t real_time_before = total_frames_before * 1000 * frame_rate.den / frame_rate.num;
    ssize_t total_frames_after = total_frames_before + cut_out - cut_in + 1;
    int64_t real_time_after = total_frames_after * 1000 * frame_rate.den / frame_rate.num;
    sprintf(buffer, "[%zd] %ld:%02ld:%02ld.%03ld (%lu) [%c] - %ld:%02ld:%02ld.%03ld (%lu) [%c]", current_cut, real_time_before/1000/3600, (real_time_before/1000/60) % 60,
            (real_time_before/1000) % 60, real_time_before % 1000, total_frames_before, av_get_picture_type_char((AVPictureType) cuts[current_cut].media_file->get_frame_info(cut_in)->frame_type),
            real_time_after/1000/3600, (real_time_after/1000/60) % 60, (real_time_after/1000) % 60, real_time_after % 1000, total_frames_after,
            av_get_picture_type_char((AVPictureType) cuts[current_cut].media_file->get_frame_info(cut_out)->frame_type));
    ui->current_cut->setText(QString(buffer));
}

void MainWindow::on_prev_cut_clicked() {
    if (current_cut <= 0) {
        return;
    }

    // save current working cut
    if (current_cut == num_cuts - 1) {
        cuts[current_cut].cut_in = cut_in;
        cuts[current_cut].cut_out = cut_out;
        cuts[current_cut].media_file = media_files[current_media_file];
    }

    current_cut--;
    change_cut();

    on_go_cut_out_clicked();
}

void MainWindow::on_next_cut_clicked() {
    if (current_cut >= num_cuts - 1) {
        return;
    }

    current_cut++;
    change_cut();

    on_go_cut_in_clicked();
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
 * Create a decode context for the video stream of the given medie file. The returned codec context must be freed manually
 * @param media_file The media file to get the video decoder for
 * @return The codec context for decoding the video stream
 */
AVCodecContext* MainWindow::get_video_decode_context(MediaFile* media_file) {
    AVStream* video_stream = media_file->get_video_stream();

    // get decoder
    const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    AVCodecContext *decode_context = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decode_context, video_stream->codecpar);
    avcodec_open2(decode_context, decoder, NULL);
    decode_context->has_b_frames = media_file->get_reorder_length();
    return decode_context;
}

/**
 * Create an encode context for the given medie file and output stream. The returned codec context must be freed manually
 * @param media_file The media file to get the video encode context for
 * @param output_stream The output stream to encode the video for
 * @return The codec context for encoding the video stream
 */
AVCodecContext* MainWindow::get_video_encode_context(MediaFile* media_file, AVStream* output_stream) {
    AVStream* video_stream = media_file->get_video_stream();
    const packet_info_t* frame_infos = media_file->get_frame_info(0);

    // get encoder
    const AVCodec* encoder = avcodec_find_encoder(output_stream->codecpar->codec_id);
    AVCodecContext* encode_context = avcodec_alloc_context3(encoder);
    avcodec_parameters_to_context(encode_context, output_stream->codecpar);
    encode_context->time_base.den = video_stream->avg_frame_rate.num;
    encode_context->time_base.num = video_stream->avg_frame_rate.den;
    encode_context->max_b_frames = media_file->get_max_bframes();
    encode_context->gop_size = media_file->get_gop_size();
    encode_context->keyint_min = media_file->get_gop_size();
    printf("gop_size: %d, keyint_min: %d\n", encode_context->gop_size, encode_context->keyint_min);

    // calculate bitrate
    if (encode_context->bit_rate == 0) {
        puts("calculating bitrate");
        ssize_t offset_diff = frame_infos[media_file->get_frame_count()-1].offset - frame_infos[0].offset;
        encode_context->bit_rate = offset_diff * 8 * video_stream->avg_frame_rate.num / video_stream->avg_frame_rate.den / media_file->get_frame_count();
    }
    printf("encoder: bitrate: %ld; global_quality: %d\n", encode_context->bit_rate, encode_context->global_quality);
    avcodec_open2(encode_context, encoder, NULL);

    return encode_context;
}

/**
 * Flush all frames from the encode context into the output context and free the encode context
 * @param encode_context Pointer to the encode context to flush
 * @param output_context The context to write the flushed frames to
 * @param stream_id The stream id of the output stream
 * @param dts The dts value of the first frame that will be flushed
 * @param frame_duration The duration of a frame
 * @return The dts value of the next frame after flushing
 */
int64_t MainWindow::flush_encode_context(AVCodecContext** encode_context, AVFormatContext* output_context, int stream_id, int64_t dts, int64_t frame_duration) {
    // preparations
    AVPacket* packet = av_packet_alloc();

    // retrieve remaining encoded packets
    avcodec_send_frame(*encode_context, NULL);
    while (avcodec_receive_packet(*encode_context, packet) == 0) {
        packet->duration = frame_duration;
        packet->dts = dts;
        dts += frame_duration;
#ifdef TRACE
        printf("Writing transcoded packet for stream %d with dts %ld, pts %ld and duration %ld\n", packet->stream_index, packet->dts, packet->pts, packet->duration);
#endif
        packet->stream_index = stream_id;
        av_interleaved_write_frame(output_context, packet);
    }

    // cleanup
    av_packet_free(&packet);
    avcodec_free_context(encode_context);

    return dts;
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
 * @return The dts of the next frame
 */
int64_t MainWindow::transcode_video_frames(MediaFile* media_file, ssize_t cut_in, ssize_t cut_out, AVFormatContext* output_context, AVStream* output_stream, int64_t start_dts, int64_t pts_offset, AVCodecContext* encode_context) {
    AVFormatContext* format_context = media_file->get_format_context();
    AVStream* video_stream = media_file->get_video_stream();
    const packet_info_t * frame_infos = media_file->get_frame_info(0);

    AVCodecContext* decode_context = get_video_decode_context(media_file);

    ssize_t iframe_before = media_file->find_iframe_before(cut_in);
    ssize_t current = iframe_before;

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
        return AV_NOPTS_VALUE;
    }
    int64_t end_pts = frame_infos[cut_out].pts + duration;
    int64_t start_pts = frame_infos[cut_in].pts;
    printf("start_pts: %ld; end_pts = %ld\n", start_pts, end_pts);
    int64_t last_pos = 0;
    int64_t end_pos = media_file->offset_after_pts(end_pts);
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
#ifdef TRACE
                    printf("skipped frame %zd with dts %ld and pts %ld\n", current, frame->pkt_dts, frame->pts);
#endif
                    current++;
                    continue;
                }

                // send frame to encoder
                // printf("got frame %zd with dts %ld and pts %ld\n", current, frame->pkt_dts, frame->pts);
                frame->pict_type = AV_PICTURE_TYPE_NONE;
                frame->pts -= pts_offset;
                // printf("frame duration: %ld\n", frame->duration);
                avcodec_send_frame(encode_context, frame);

                // retrieve encoded packets
                while (avcodec_receive_packet(encode_context, packet) == 0) {
                    packet->duration = duration;
                    packet->dts = dts;
                    dts += duration;
#ifdef TRACE
                    printf("Writing transcoded packet for stream %d with dts %ld, pts %ld and duration %ld\n", packet->stream_index, packet->dts, packet->pts, packet->duration);
#endif
                    packet->stream_index = output_stream->index;
                    av_interleaved_write_frame(output_context, packet);
                }

                current++;
            }
        }
        av_packet_unref(packet);
    }

    // cleanup
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decode_context);

    return dts;
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

    // analyze streams
    for (int i = 0; i < format_context->nb_streams; i++)
    {
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

    // preparations
    int64_t* next_dts = (int64_t*) malloc(sizeof(int64_t) * format_context->nb_streams);
    for (int i = 0; i < format_context->nb_streams; i++) {
        next_dts[i] = AV_NOPTS_VALUE;
    }
    int64_t* audio_desync = (int64_t*) malloc(sizeof(int64_t) * format_context->nb_streams);
    memset(audio_desync, 0, sizeof(int64_t) * format_context->nb_streams);
    int64_t next_video_pts = cuts[0].media_file->get_frame_info(cuts[0].cut_in)->pts;
    AVCodecContext* encode_context = NULL;

    // iterate over all cuts
    // skip last "cut", since we use it to store the cut that is currently composed
    for (int i = 0; i < num_cuts - 1; i++) {
        puts("================================");
        // get infos
        AVFormatContext* format_context = cuts[i].media_file->get_format_context();
        AVStream* video_stream = cuts[i].media_file->get_video_stream();
        const packet_info_t * frame_infos = cuts[i].media_file->get_frame_info(0);

        // get timings
        ssize_t remux_start = cuts[i].media_file->find_iframe_after(cuts[i].cut_in);
        ssize_t remux_end = cuts[i].media_file->find_pframe_before(cuts[i].cut_out);
        int64_t pts_offset = cuts[i].media_file->get_frame_info(cuts[i].cut_in)->pts - next_video_pts;
        printf("computed offset: %ld\n", pts_offset);
        ssize_t first_iframe = remux_start;
        // printf("computed remux values: %ld/%ld/%ld\n", remux_start, remux_end, remux_save_end);

        // fix small cuts
        if (remux_start > cuts[i].cut_out) {
            remux_start = cuts[i].cut_out + 1;
            remux_end = cuts[i].cut_out;
            puts("fixed small cut");
        }
        printf("cut_in: %zd; remux_start: %zd\n", cuts[i].cut_in, remux_start);

        int64_t unused_dts = 0;
        for (int j = remux_start - 1; j >= 0 && !frame_infos[j].is_keyframe; j--) {
            if (frame_infos[j].dts > frame_infos[remux_start].dts) {
                unused_dts += frame_infos[j].duration;
            }
        }
        printf("unused dts: %ld\n", unused_dts);

        // transcode frames before first i-frame
        if (cuts[i].cut_in < remux_start) {
            if (encode_context == NULL) {
                encode_context = get_video_encode_context(cuts[i].media_file, output_video_stream);
            }

            if (next_dts[video_stream->index] == AV_NOPTS_VALUE) {
                next_dts[video_stream->index] = frame_infos[first_iframe].dts - (frame_infos[first_iframe].pts - frame_infos[cuts[i].cut_in].pts) + unused_dts;
            }
            next_dts[video_stream->index] = transcode_video_frames(cuts[i].media_file, cuts[i].cut_in, remux_start-1, output_context, output_video_stream, next_dts[video_stream->index], pts_offset, encode_context);
        }

        // flush encode context
        if (encode_context != NULL) {
            next_dts[video_stream->index] = flush_encode_context(&encode_context, output_context, output_video_stream->index, next_dts[video_stream->index], frame_infos[remux_end].duration);
        }

        // remux frames between first i-frame and last p-frame
        AVPacket *packet = av_packet_alloc();
        int64_t packet_length_dts = frame_infos[cuts[i].cut_in].duration;
        long start_pts = frame_infos[cuts[i].cut_in].pts;
        long end_pts = frame_infos[cuts[i].cut_out].pts + packet_length_dts;
        long remux_start_pts = frame_infos[remux_start].pts;
        long remux_end_pts = frame_infos[remux_end].pts + packet_length_dts;

        // calculate audio desync
        if (i > 0) {
            for (int j = 0; j < format_context->nb_streams; j++) {
                if (cuts[i].media_file->is_audio_stream(j)) {
                    const packet_info_t* info = cuts[i].media_file->get_packet_info(j, start_pts);
                    if (info == NULL) {
                        continue;
                    }
                    audio_desync[j] = next_dts[j] - (info->pts - pts_offset);
                    if (audio_desync[j] < info->duration / -2) {
                        audio_desync[j] += info->duration;
                    }
                    printf("audio_desync for stream %d: %ld\n", j, audio_desync[j]);
                }
            }
        }

        // seek to start
        int64_t offset = cuts[i].media_file->offset_before_pts(frame_infos[cuts[i].cut_in].pts);
        if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
            puts("Seek failed");
            return;
        }
        int64_t last_offset = offset;
        int64_t loop_end = cuts[i].media_file->offset_after_pts(end_pts);
        printf("Looping from %ld to %ld\n", offset, loop_end);
        printf("new pts: %ld to %ld\n", remux_start_pts, remux_end_pts);
        while (last_offset <= loop_end) {
            if (av_read_frame(format_context, packet)) {
                puts("failed to read packet");
                break;
            }
#ifdef TRACE
                printf("Read packet for stream %d with dts %ld, pts %ld and duration %ld\n", packet->stream_index, packet->dts, packet->pts, packet->duration);
#endif
            last_offset = packet->pos;
            if (cuts[i].media_file->is_audio_stream(packet->stream_index)) {
                packet->pts += audio_desync[packet->stream_index];
                packet->dts += audio_desync[packet->stream_index];
            }
            bool write_packet = false;
            if (index[packet->stream_index] != -1) {
                if (packet->stream_index == video_stream->index) {
                    write_packet = packet->pts >= remux_start_pts && packet->pts + packet->duration <= remux_end_pts;
                    if (packet->pts == remux_start_pts) {
                        packet->dts += unused_dts;
                    }
                } else if (packet->dts - pts_offset >= next_dts[packet->stream_index]) {
                    write_packet = packet->pts + packet->duration <= end_pts;
                    if (!write_packet && cuts[i].media_file->is_audio_stream(packet->stream_index) && i < num_cuts - 2) {
                        write_packet = packet->pts + packet->duration / 2 < end_pts;
                    }
                }
            }
            if (write_packet) {
                packet->pts -= pts_offset;
                packet->dts -= pts_offset;
                next_dts[packet->stream_index] = packet->dts + packet->duration;
#ifdef TRACE
                printf("Writing packet for stream %d with dts %ld, pts %ld and duration %ld\n", packet->stream_index, packet->dts, packet->pts, packet->duration);
#endif
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
            encode_context = get_video_encode_context(cuts[i].media_file, output_video_stream);
            next_dts[video_stream->index] = transcode_video_frames(cuts[i].media_file, remux_end+1, cuts[i].cut_out, output_context, output_video_stream, next_dts[video_stream->index], pts_offset, encode_context);
        }
        next_video_pts = end_pts - pts_offset;
    }

    // flush encode context
    if (encode_context != NULL) {
        flush_encode_context(&encode_context, output_context, output_video_stream->index, next_dts[video_stream->index], frame_infos[0].duration);
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

