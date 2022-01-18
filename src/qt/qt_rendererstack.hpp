#ifndef QT_RENDERERCONTAINER_HPP
#define QT_RENDERERCONTAINER_HPP

#include <QStackedWidget>
#include <QKeyEvent>
#include <QEvent>
#include <memory>
#include <vector>
#include <atomic>
#include <tuple>

#ifdef WAYLAND
#include "wl_mouse.hpp"
#endif

#ifdef EVDEV_INPUT
#include "evdev_mouse.hpp"
#endif

#ifdef __APPLE__
#include "cocoa_mouse.hpp"
#endif

#ifdef __ANDROID__
#include "android_mouse.hpp"
#endif

namespace Ui {
class RendererStack;
}

class RendererCommon;
class RendererStack : public QStackedWidget
{
    Q_OBJECT

public:
    explicit RendererStack(QWidget *parent = nullptr);
    ~RendererStack();

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent* event) override
    {
        event->ignore();
    }
    void keyReleaseEvent(QKeyEvent* event) override
    {
        event->ignore();
    }
    bool event(QEvent* event) override;

    enum class Renderer {
        Software,
        OpenGL,
        OpenGLES,
        OpenGL3
    };
    void switchRenderer(Renderer renderer);

    RendererCommon* rendererWindow{nullptr};
signals:
    void blitToRenderer(int buf_idx, int x, int y, int w, int h);

public slots:
    void blit(int x, int y, int w, int h);
    void mousePoll();

private:
    Ui::RendererStack *ui;

    struct mouseinputdata {
        int deltax, deltay, deltaz;
        int mousebuttons;
    };
    mouseinputdata mousedata;

    struct mouseinputbackend
    {
        std::function<void()> init = []{};
        std::function<void(QWindow*)> capture_mouse = [](QWindow*){};
        std::function<void()> uncapture_mouse = []{};
        std::function<void()> poll_mouse = []{};
    };

    QMap<QString, mouseinputbackend> mouse_input_backends
    {
#ifdef WAYLAND
        {"wayland", {wl_init, wl_mouse_capture, wl_mouse_uncapture, wl_mouse_poll}},
#endif
#ifdef EVDEV_INPUT
        {"xcb", {evdev_init, evdev_mouse_capture, evdev_mouse_uncapture, evdev_mouse_poll}},
        {"eglfs", {evdev_init, evdev_mouse_capture, evdev_mouse_uncapture, evdev_mouse_poll}},
#endif
#ifdef __APPLE__
        {"cocoa", {macos_init, macos_mouse_capture, macos_mouse_uncapture, macos_poll_mouse}},
#endif
#ifdef __ANDROID__
        {"android", {android_init, android_mouse_capture, android_mouse_uncapture, android_mouse_poll}},
#endif
        {"", mouseinputbackend()}
    };

    int x, y, w, h, sx, sy, sw, sh;
    uint8_t touchInProgress = 0;
    uint8_t touchTap = 0;
    bool touchUpdated = false;
    bool touchMoveMouse = false;
    int currentBuf = 0;
    std::vector<std::tuple<uint8_t*, std::atomic_flag*>> imagebufs;

    std::unique_ptr<QWidget> current;

    friend class MainWindow;
};

#endif // QT_RENDERERCONTAINER_HPP
