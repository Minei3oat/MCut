#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "mediafile.h"

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
    void on_go_cut_in_clicked();
    void on_go_cut_out_clicked();

    void on_prev_frame_2_clicked();
    void on_next_frame_2_clicked();

    void on_position_slider_sliderMoved(int position);

private:
    void render_frame();
    void transcode_video_frames(AVFormatContext *format_context, AVStream *video_stream, frame_info_t *frame_infos, ssize_t cut_in, ssize_t cut_out, AVFormatContext *output_context, AVStream *output_stream, int64_t start_dts, int *index, int precut);

    Ui::MainWindow *ui;

    MediaFile *media_file = NULL;

    // store important infos here for testing
    long current_frame = -1;
    ssize_t cut_in = -1;
    ssize_t cut_out = -1;
};
#endif // MAINWINDOW_H
