#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <stdio.h>
#include <sys/mman.h>

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
    // cleanup
    if (format_context) {
        avformat_free_context(format_context);
    }

    // preparations
    format_context = avformat_alloc_context();

    // select file
    std::string filename = QFileDialog::getOpenFileName(this, "Open Video").toStdString();

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
        }
        // general
        printf("\tCodec %s ID %d bit_rate %ld\n", local_codec->long_name, local_codec->id, local_codec_parameters->bit_rate);
        printf("\tDuration %ld us; timebase: %d/%d\n", stream->duration,  stream->time_base.num, stream->time_base.den);
    }

    cache_frame_infos();

    current_frame = 0;
    render_frame();
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


void MainWindow::render_frame()
{
    if (!video_stream || current_frame >= frame_count) {
        return;
    }

    // get decoder
    const AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context, video_stream->codecpar);
    avcodec_open2(codec_context, codec, NULL);

    // preparations
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // find keyframe
    int current = find_iframe_before(frame_infos, current_frame);
    printf("starting decoding at frame %d\n", current);

    // get frame
    int64_t offset = frame_infos[current].offset;
    if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
        puts("Seek failed");
        return;
    }
    while(current <= current_frame) {
        if (av_read_frame(format_context, packet)) {
            puts("failed to read packet");

            // flush decoder
            avcodec_send_packet(codec_context, NULL);
            while (current <= current_frame && avcodec_receive_frame(codec_context, frame) == 0) {
                printf("got frame %d\n", current);
                current++;
            }
            break;
        }
        if (packet->stream_index == video_stream->index) {
            avcodec_send_packet(codec_context, packet);
            while (current <= current_frame && avcodec_receive_frame(codec_context, frame) == 0) {
                printf("got frame %d\n", current);
                current++;
            }
        }
        av_packet_unref(packet);
    }
    if (frame->data[0] == NULL) {
        puts("data[0] is NULL");
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec_context);
        return;
    }

    // convert frame to RGB
    AVFrame *rgb = av_frame_alloc();
    rgb->width = codec_context->width;
    rgb->height = codec_context->height;
    rgb->format = AV_PIX_FMT_RGB24;
    av_image_alloc(rgb->data, rgb->linesize, rgb->width, rgb->height, (AVPixelFormat)rgb->format, 1);
    struct SwsContext *sws_context = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, rgb->width, rgb->height, (AVPixelFormat)rgb->format, SWS_BILINEAR, NULL, NULL, NULL);
    sws_scale_frame(sws_context, rgb, frame);

    // render frame
    QImage image(rgb->data[0], rgb->width, rgb->height, QImage::Format_RGB888);
    QPixmap pm(QPixmap::fromImage(image));
    ui->video_frame->setPixmap(pm);
    ui->video_frame->setScaledContents(true);
    ui->video_frame->update();
    printf("rendered frame %lu\n", current_frame);

    // update current position
    char buffer[64] = "";
    sprintf(buffer, "%lu [%c]", current_frame, av_get_picture_type_char(frame->pict_type));
    ui->current_pos->setText(QString(buffer));

    // cleanup
    sws_freeContext(sws_context);
    av_frame_free(&frame);
    av_frame_free(&rgb);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
}

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

            frame_info_t *destination = current;
            while (destination > frame_infos && (destination-1)->pts > packet->pts) {
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
            av_packet_unref(packet);
        }
    }

    current = frame_infos;
    for (unsigned long i = 0; i < frame_count; i++, current++) {
        printf("found frame %lu at %lu with pts %ld and dts %ld; is key: %d; is corrupt: %d\n", i, current->offset, current->pts, current->dts, current->is_keyframe, current->is_corrupt);
    }

    av_packet_free(&packet);
}
ssize_t MainWindow::find_iframe_before(frame_info_t *frame_infos, ssize_t search)
{
    ssize_t iframe_before = search;
    while (iframe_before >= 0 && !frame_infos[iframe_before].is_keyframe) {
        iframe_before--;
    }
    return iframe_before;
}


ssize_t MainWindow::find_iframe_after(frame_info_t *frame_infos, ssize_t search, ssize_t frame_count)
{
    ssize_t iframe_after = search;
    while (iframe_after < frame_count && !frame_infos[iframe_after].is_keyframe) {
        iframe_after++;
    }
    return iframe_after >= frame_count ? -1 : iframe_after;
}


    av_packet_free(&packet);
}
