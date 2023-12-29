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

    void on_prev_frame_clicked();
    void on_next_frame_clicked();

    void on_prev_frame_2_clicked();
    void on_next_frame_2_clicked();

private:
    void render_frame();
    void cache_frame_infos();
    ssize_t find_iframe_before(frame_info_t *frame_infos, ssize_t search);
    ssize_t find_iframe_after(frame_info_t *frame_infos, ssize_t search, ssize_t frame_count);

    Ui::MainWindow *ui;

    // store important infos here for testing
    AVFormatContext *format_context = NULL;
    AVStream *video_stream = NULL;
    long current_frame = -1;
    unsigned long frame_count = 0;
    frame_info_t *frame_infos = NULL;
};
#endif // MAINWINDOW_H
