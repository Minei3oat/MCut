#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "mediafile.h"

#define MAX_MEDIA_FILES 32
#define MAX_CUTS 64

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

typedef struct cut {
    MediaFile* media_file = NULL;
    ssize_t cut_in = -1;
    ssize_t cut_out = -1;
} cut_t;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void on_actionOpen_Video_triggered();
    void on_actionCut_Video_triggered();
    void on_actionExit_triggered();

    void on_prev_media_file_clicked();
    void on_next_media_file_clicked();

    void on_prev_cut_clicked();
    void on_next_cut_clicked();

    void on_prev_frame_clicked();
    void on_next_frame_clicked();
    void on_prev_frame_2_clicked();
    void on_next_frame_2_clicked();

    void on_set_cut_in_clicked();
    void on_set_cut_out_clicked();
    void on_go_cut_in_clicked();
    void on_go_cut_out_clicked();

    void on_add_cut_clicked();
    void on_delete_cut_clicked() {}

    void on_position_slider_sliderMoved(int position);
    void on_jump_to_frame_returnPressed();

private:
    void render_frame();
    void change_media_file();
    void change_cut();

    AVCodecContext* get_video_decode_context(MediaFile* media_file);
    AVCodecContext* get_video_encode_context(MediaFile* media_file, AVStream* output_stream);
    int64_t flush_encode_context(AVCodecContext** encode_context, AVFormatContext* output_context, int stream_id, int64_t dts, int64_t frame_duration);
    int64_t transcode_video_frames(MediaFile* media_file, ssize_t cut_in, ssize_t cut_out, AVFormatContext* output_context, AVStream* output_stream, int64_t start_dts, int64_t pts_offset, AVCodecContext* encode_context);

    Ui::MainWindow *ui;

    MediaFile* media_files[MAX_MEDIA_FILES] = { };
    ssize_t current_media_file = -1;
    ssize_t num_media_files = 0;

    cut_t cuts[MAX_CUTS];
    ssize_t current_cut = 0;
    ssize_t num_cuts = 1;
    ssize_t cut_in = 0;
    ssize_t cut_out = 0;
};
#endif // MAINWINDOW_H
