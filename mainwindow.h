#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

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

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_actionOpen_Video_triggered();
    void on_actionCut_Video_triggered();
    void on_actionExit_triggered();

    void on_prev_frame_clicked();
    void on_next_frame_clicked();

    void on_set_cut_in_clicked();
    void on_set_cut_out_clicked();

    void on_prev_frame_2_clicked();
    void on_next_frame_2_clicked();

private:
    void render_frame();
    AVFrame* get_frame(AVStream *stream, ssize_t frame_index);
    void cache_frame_infos();
    ssize_t find_iframe_before(frame_info_t *frame_infos, ssize_t search);
    ssize_t find_pframe_before(frame_info_t *frame_infos, ssize_t search);
    ssize_t find_iframe_after(frame_info_t *frame_infos, ssize_t search, ssize_t frame_count);
    ssize_t find_pframe_after(frame_info_t *frame_infos, ssize_t search, ssize_t frame_count);
    void transcode_video_frames(AVFormatContext *format_context, AVStream *video_stream, frame_info_t *frame_infos, ssize_t cut_in, ssize_t cut_out, AVFormatContext *output_context, AVStream *output_stream, int64_t start_dts);

    Ui::MainWindow *ui;

    // store important infos here for testing
    AVFormatContext *format_context = NULL;
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;
    long current_frame = -1;
    unsigned long frame_count = 0;
    int reorder_length = 0;
    int max_bframes = 0;
    ssize_t cut_in = -1;
    ssize_t cut_out = -1;
    frame_info_t *frame_infos = NULL;
};
#endif // MAINWINDOW_H
