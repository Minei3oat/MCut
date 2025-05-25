#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <QFileDialog>

// #define TRACE
// #define WITHOUT_TELETEXT

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->statusbar->addWidget(&total_length_label, 1);
    refresh_total_length();
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

void MainWindow::on_prev_frame_3_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    MediaFile* media_file = media_files[current_media_file];
    media_file->current_frame -= 12;
    if (media_file->current_frame < 0) {
        media_file->current_frame = 0;
    }
    render_frame();
}

void MainWindow::on_next_frame_3_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    MediaFile* media_file = media_files[current_media_file];
    media_file->current_frame += 12;
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
        target = media_file->find_iframe_before(position);
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
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    ssize_t frame_count = media_files[current_media_file]->get_frame_count();
    if (cut_in < frame_count) {
        media_files[current_media_file]->current_frame = cut_in;
    } else {
        media_files[current_media_file]->current_frame = frame_count - 1;
    }
    render_frame();
}

void MainWindow::on_go_cut_out_clicked()
{
    if (current_media_file < 0 || current_media_file >= num_media_files) {
        return;
    }

    ssize_t frame_count = media_files[current_media_file]->get_frame_count();
    if (cut_out < frame_count) {
        media_files[current_media_file]->current_frame = cut_out;
    } else {
        media_files[current_media_file]->current_frame = frame_count - 1;
    }
    render_frame();
}

int MainWindow::sprint_frametime(char* buffer, ssize_t index) {
    int64_t real_time = 0;
    if (index > 0) {
        AVRational frame_rate = media_files[0]->get_video_stream()->avg_frame_rate;
        real_time = index * 1000 * frame_rate.den / frame_rate.num;
    }
    return sprintf(buffer, "%ld:%02ld:%02ld.%03ld", real_time/1000/3600, (real_time/1000/60) % 60, (real_time/1000) % 60, real_time % 1000);
}

QString MainWindow::frame_to_string(MediaFile* media_file, ssize_t index) {
    char buffer[64] = "";

    int current = sprint_frametime(buffer, index);
    const packet_info_t* info = media_file->get_frame_info(index);
    sprintf(buffer + current, " - %lu [%c] (%ld)", index, av_get_picture_type_char((AVPictureType) info->frame_type), info->pts);

    return QString(buffer);
}

QString MainWindow::cut_to_string(ssize_t index) {
    char buffer[128] = "";

    ssize_t total_frames_before = 0;
    for (int i = 0; i < index; i++) {
        total_frames_before += cuts[i].cut_out - cuts[i].cut_in + 1;
    }
    ssize_t total_frames_after = total_frames_before + cut_out - cut_in + 1;
    int current = sprintf(buffer, "[%zd] ", index);
    current += sprint_frametime(buffer + current, total_frames_before);
    current += sprintf(buffer + current, " (%lu) [%c] - ", total_frames_before, av_get_picture_type_char((AVPictureType) cuts[index].media_file->get_frame_info(cut_in)->frame_type));
    current += sprint_frametime(buffer + current, total_frames_after);
    current += sprintf(buffer + current, " (%lu) [%c]", total_frames_after, av_get_picture_type_char((AVPictureType) cuts[index].media_file->get_frame_info(cut_out)->frame_type));

    return QString(buffer);
}

void MainWindow::refresh_total_length() {
    char buffer[64] = "";
    ssize_t total_frames = 0;
    for (int i = 0; i < num_cuts - 1; i++) {
        total_frames += cuts[i].cut_out - cuts[i].cut_in + 1;
    }
    int current = sprintf(buffer, "%zd cuts - ", num_cuts - 1);
    current += sprint_frametime(buffer + current, total_frames);
    sprintf(buffer + current, " (%ld)", total_frames);
    total_length_label.setText(QString(buffer));
}

void MainWindow::on_add_cut_clicked() {
    if (current_media_file < 0 || current_media_file >= num_media_files || num_cuts >= MAX_CUTS || cut_in > cut_out) {
        return;
    }

    ssize_t frame_count = media_files[current_media_file]->get_frame_count();
    if (cut_in < 0 || cut_out < 0 || cut_in >= frame_count || cut_in >= frame_count) {
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
    refresh_total_length();

    // show added cut time
    ui->current_cut->setText(cut_to_string(current_cut - 1));
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
    ui->current_cut->setText(cut_to_string(current_cut));
    ui->cut_in_pos->setText(frame_to_string(cuts[current_cut].media_file, cut_in));
    ui->cut_out_pos->setText(frame_to_string(cuts[current_cut].media_file, cut_out));
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

    // convert frame to RGB and mind aspect ratio
    AVFrame *rgb = av_frame_alloc();
    rgb->format = AV_PIX_FMT_RGB24;
    if (frame->sample_aspect_ratio.num > frame->sample_aspect_ratio.den) {
        rgb->width = frame->width * frame->sample_aspect_ratio.num / frame->sample_aspect_ratio.den;
        rgb->height = frame->height;
    } else {
        rgb->width = frame->width;
        rgb->height = frame->height * frame->sample_aspect_ratio.den / frame->sample_aspect_ratio.num;
    }
    av_frame_get_buffer(rgb, 4);
    struct SwsContext *sws_context = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, rgb->width, rgb->height, (AVPixelFormat)rgb->format, SWS_BILINEAR, NULL, NULL, NULL);
    sws_scale_frame(sws_context, rgb, frame);

    // render frame
    QImage image(rgb->data[0], rgb->width, rgb->height, QImage::Format_RGB888);
    QPixmap pm(QPixmap::fromImage(image).scaled(ui->video_frame->width(), ui->video_frame->height(), Qt::KeepAspectRatio));
    ui->video_frame->setPixmap(pm);
    ui->video_frame->update();

    // update current position
    ui->current_pos->setText(frame_to_string(media_file, media_file->current_frame));

    // cleanup
    sws_freeContext(sws_context);
    av_frame_free(&frame);
    av_frame_free(&rgb);

    gettimeofday(&end, NULL);
    // printf("rendered frame in %lu us\n", end.tv_usec-start.tv_usec);
}

/**
 * Write a packet to the output stream
 * @param output_context The format context to write the packet to
 * @param packet The packet to write
 * @return The result from av_interleaved_write_frame
 */
int MainWindow::write_packet(AVFormatContext* output_context, AVPacket* packet) {
    av_packet_rescale_ts(packet, media_files[0]->get_video_stream()->time_base, output_context->streams[0]->time_base);
#ifdef TRACE
    printf("Writing output packet for stream %d with dts %ld, pts %ld and duration %ld\n", packet->stream_index, packet->dts, packet->pts, packet->duration);
#endif
    return av_interleaved_write_frame(output_context, packet);
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
        write_packet(output_context, packet);
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
                    write_packet(output_context, packet);
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
    output_video_stream->codecpar->codec_tag = 0;
    output_video_stream->avg_frame_rate = video_stream->avg_frame_rate;
    output_video_stream->time_base = video_stream->time_base;
    printf("video: %d/%d, codec: %d/%d\n", video_stream->sample_aspect_ratio.num, video_stream->sample_aspect_ratio.den, video_stream->codecpar->sample_aspect_ratio.num, video_stream->codecpar->sample_aspect_ratio.den);
    if (output_video_stream->sample_aspect_ratio.num == 0) {
        output_video_stream->sample_aspect_ratio = output_video_stream->codecpar->sample_aspect_ratio;
    }
    printf("video: %d/%d, codec: %d/%d\n", output_video_stream->sample_aspect_ratio.num, output_video_stream->sample_aspect_ratio.den, output_video_stream->codecpar->sample_aspect_ratio.num, output_video_stream->codecpar->sample_aspect_ratio.den);
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
#ifdef WITHOUT_TELETEXT
        AVCodecParameters *local_codec_parameters = input_stream->codecpar;
        const AVCodec *local_codec = avcodec_find_decoder(local_codec_parameters->codec_id);
        if (local_codec == NULL) {
            printf("No codec found\n");
            continue;
        }
#endif

        // copy audio and DVB subtitles
#ifdef WITHOUT_TELETEXT
        if (local_codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO
            || (local_codec_parameters->codec_type == AVMEDIA_TYPE_SUBTITLE && local_codec_parameters->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
        ) {
#endif
            AVStream* output_stream = avformat_new_stream(output_context, NULL);
            avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar);
            output_stream->codecpar->codec_tag = 0;
            output_stream->disposition = input_stream->disposition;
            av_dict_copy(&output_stream->metadata, input_stream->metadata, 0);
            index[input_stream->index] = output_stream->index;
#ifdef WITHOUT_TELETEXT
        }
#endif
    }

    // set max interleave delta
    output_context->max_interleave_delta = 0;
    for (ssize_t i = 0; i < cuts[0].media_file->get_frame_count(); i++) {
        if (frame_infos[i].pts - frame_infos[i].dts > output_context->max_interleave_delta) {
            output_context->max_interleave_delta = frame_infos[i].pts - frame_infos[i].dts;
        }
    }

    // write header
    avio_open(&output_context->pb, filename.c_str(), AVIO_FLAG_WRITE);
    if (avformat_write_header(output_context, NULL) < 0) {
        puts("Failed writing header");
        avio_closep(&output_context->pb);
        avformat_free_context(output_context);
        return;
    }

    printf("original - frame rate: %d/%d; time_base: %d/%d\n", video_stream->avg_frame_rate.num, video_stream->avg_frame_rate.den, video_stream->time_base.num, video_stream->time_base.den);
    printf("output   - frame rate: %d/%d; time_base: %d/%d\n", output_video_stream->avg_frame_rate.num, output_video_stream->avg_frame_rate.den, output_video_stream->time_base.num, output_video_stream->time_base.den);

    // preparations
    int64_t* next_pts = (int64_t*) calloc(format_context->nb_streams, sizeof(int64_t));
    int64_t* audio_desync = (int64_t*) calloc(format_context->nb_streams, sizeof(int64_t));
    int64_t next_video_dts = 0;
    AVCodecContext* encode_context = NULL;
    output_context->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE;

    // determine max GOP size and needed difference between dts and pts
    int64_t max_gop_size = 0;
    for (int i = 0; i < num_cuts - 1; i++) {
        if (cuts[i].media_file->get_max_difference() > next_pts[video_stream->index]) {
            next_pts[video_stream->index] = cuts[i].media_file->get_max_difference();
        }
        if (max_gop_size < cuts[i].media_file->get_gop_size()) {
            max_gop_size = cuts[i].media_file->get_gop_size();
        }
    }
    printf("max GOP size: %ld\n", max_gop_size);
    output_context->max_interleave_delta += 2*max_gop_size*cuts[0].media_file->get_frame_info(0)->duration;

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
        int64_t pts_offset = cuts[i].media_file->get_frame_info(cuts[i].cut_in)->pts - next_pts[video_stream->index];
#ifdef TRACE
        printf("computed offset: %ld\n", pts_offset);
        printf("computed remux values: %ld/%ld\n", remux_start, remux_end);
#endif

        // fix small cuts
        if (remux_start > cuts[i].cut_out) {
            remux_start = cuts[i].cut_out + 1;
            remux_end = cuts[i].cut_out;
            puts("fixed small cut");
        }

        int64_t unused_dts = 0;
        for (int j = remux_start - 1; j >= 0 && !frame_infos[j].is_keyframe; j--) {
            if (frame_infos[j].dts > frame_infos[remux_start].dts) {
                unused_dts += frame_infos[j].duration;
            }
        }
        printf("unused dts: %ld\n", unused_dts);

        // calculate first pts
        if (i == 0) {
            next_pts[video_stream->index] -= unused_dts;
            for (int j = 0; j < format_context->nb_streams; j++) {
                next_pts[j] = next_pts[video_stream->index];
            }
            printf("first pts: %ld\n", next_pts[video_stream->index]);
        }

        // transcode frames before first i-frame
        if (cuts[i].cut_in < remux_start) {
            if (encode_context == NULL) {
                encode_context = get_video_encode_context(cuts[i].media_file, output_video_stream);
            }
            next_video_dts = transcode_video_frames(cuts[i].media_file, cuts[i].cut_in, remux_start-1, output_context, output_video_stream, next_video_dts, pts_offset, encode_context);
        }

        // flush encode context
        if (encode_context != NULL) {
            next_video_dts = flush_encode_context(&encode_context, output_context, output_video_stream->index, next_video_dts, frame_infos[remux_end].duration);
        }

        // remux frames between first i-frame and last p-frame
        AVPacket *packet = av_packet_alloc();
        int64_t packet_length_dts = frame_infos[cuts[i].cut_in].duration;
        long start_pts = frame_infos[cuts[i].cut_in].pts;
        long end_pts = frame_infos[cuts[i].cut_out].pts + packet_length_dts;
        long remux_start_pts = frame_infos[remux_start].pts;
        long remux_end_pts = frame_infos[remux_end].pts + packet_length_dts;
        printf("cut_in: %zd (%ld); remux_start: %zd (%ld)\n", cuts[i].cut_in, start_pts, remux_start, remux_start_pts);
        printf("cut_out: %zd (%ld); remux_end: %zd (%ld)\n", cuts[i].cut_out, end_pts, remux_end, remux_end_pts);

        // calculate audio desync
        int64_t margin = packet_length_dts;
        if (i > 0) {
            for (int j = 0; j < format_context->nb_streams; j++) {
                if (cuts[i].media_file->is_audio_stream(j)) {
                    const packet_info_t* info = cuts[i].media_file->get_packet_info(j, start_pts);
                    if (info == NULL) {
                        continue;
                    }
                    audio_desync[j] = next_pts[j] - (info->pts - pts_offset);
                    if (audio_desync[j] < info->duration / -2) {
                        audio_desync[j] += info->duration;
                    }
                    if (audio_desync[j] > info->duration / 2) {
                        audio_desync[j] -= info->duration;
                    }
                    printf("audio_desync for stream %d: %ld\n", j, audio_desync[j]);
                    if (info->duration > margin) {
                        margin = info->duration;
                    }
                }
            }
        }

        for (int j = 0; j < format_context->nb_streams; j++) {
            printf("next pts (stream %d): %ld\n", j, next_pts[j]);
        }

        // seek to start
        int64_t offset = cuts[i].media_file->offset_before_pts(frame_infos[cuts[i].cut_in].pts - margin);
        if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
            puts("Seek failed");
            return;
        }
        int64_t last_offset = offset;
        int64_t loop_end = cuts[i].media_file->offset_after_pts(end_pts + margin);
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
            bool do_write_packet = false;
            if (packet->pts == AV_NOPTS_VALUE || packet->dts == AV_NOPTS_VALUE) {
                printf("Read packet for stream %d without dts/pts (next pts: %ld)\n", packet->stream_index, next_pts[packet->stream_index] + pts_offset);
            } else if (index[packet->stream_index] != -1) {
                if (packet->stream_index == video_stream->index) {
                    do_write_packet = packet->pts >= remux_start_pts && packet->pts + packet->duration <= remux_end_pts;
                    if (packet->pts == remux_start_pts) {
                        packet->dts += unused_dts;
                    }
                } else if (packet->pts - pts_offset >= next_pts[packet->stream_index]) {
                    do_write_packet = packet->pts + packet->duration <= end_pts;
                    if (!do_write_packet && cuts[i].media_file->is_audio_stream(packet->stream_index) && i < num_cuts - 2) {
                        do_write_packet = packet->pts + packet->duration / 2 < end_pts;
                    }
                }
            }
            if (do_write_packet) {
                packet->pts -= pts_offset;
                packet->dts -= pts_offset;
                next_pts[packet->stream_index] = packet->pts + packet->duration;
                if (packet->stream_index == video_stream->index) {
                    next_video_dts = packet->dts + packet_length_dts;
                }
#ifdef TRACE
                printf("Writing packet for stream %d with dts %ld, pts %ld and duration %ld\n", packet->stream_index, packet->dts, packet->pts, packet->duration);
#endif
                packet->stream_index = index[packet->stream_index];
                write_packet(output_context, packet);
            }
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        printf("original - frame rate: %d/%d; time_base: %d/%d\n", video_stream->avg_frame_rate.num, video_stream->avg_frame_rate.den, video_stream->time_base.num, video_stream->time_base.den);
        printf("output   - frame rate: %d/%d; time_base: %d/%d\n", output_video_stream->avg_frame_rate.num, output_video_stream->avg_frame_rate.den, output_video_stream->time_base.num, output_video_stream->time_base.den);

        if (remux_end < cuts[i].cut_out) {
            // transcode frames after last p-frame
            encode_context = get_video_encode_context(cuts[i].media_file, output_video_stream);
            next_video_dts = transcode_video_frames(cuts[i].media_file, remux_end+1, cuts[i].cut_out, output_context, output_video_stream, next_video_dts, pts_offset, encode_context);
        }
        next_pts[video_stream->index] = end_pts - pts_offset;
    }

    // flush encode context
    if (encode_context != NULL) {
        flush_encode_context(&encode_context, output_context, output_video_stream->index, next_video_dts, frame_infos[0].duration);
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

