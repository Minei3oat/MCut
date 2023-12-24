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
    current_frame++;
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

    // get frame
    int current = current_frame;
    while (!frame_infos[current].is_keyframe) {
        current--;
        if (current < 0) {
            printf("found no packet with id %ld\n", current_frame);
            return;
        }
    }
    printf("starting decoding at frame %d\n", current);

    int64_t offset = frame_infos[current].offset;
    if (avformat_seek_file(format_context, video_stream->index, offset-64, offset, offset+64, AVSEEK_FLAG_BYTE) < 0) {
        printf("Seek failed\n");
        return;
    }
    while(current <= current_frame) {
        if (av_read_frame(format_context, packet)) {
            printf("failed to read packet\n");
            break;
        }
        if (packet->stream_index == video_stream->index) {
            printf("found packet for stream\n");
            avcodec_send_packet(codec_context, packet);
            while (current <= current_frame && avcodec_receive_frame(codec_context, frame) == 0) {
                printf("got frame %d\n", current);
                current++;
            }
        }
        av_packet_unref(packet);
        printf("going to next packet\n");
    }
    if (frame->data[0] == NULL) {
        printf("data[0] is NULL\n");
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
    const AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context, video_stream->codecpar);
    avcodec_open2(codec_context, codec, NULL);

    // preparations
    AVPacket *packet = av_packet_alloc();

    frame_count = 0;
    frame_info_t *current = frame_infos;
    while (av_read_frame(format_context, packet) == 0) {
        if (packet->stream_index == video_stream->index) {
            current->offset = packet->pos;
            current->is_keyframe = packet->flags & AV_PKT_FLAG_KEY;
            printf("found frame at %lu; is key: %d\n", current->offset, current->is_keyframe);
            current++;
            frame_count++;
            av_packet_unref(packet);
        }
    }

    av_packet_free(&packet);
}
