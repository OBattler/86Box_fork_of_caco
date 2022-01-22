#include "qt_rendererstack.hpp"
#include "ui_qt_rendererstack.h"

#include "qt_softwarerenderer.hpp"
#include "qt_hardwarerenderer.hpp"

#include "qt_mainwindow.hpp"

#include "evdev_mouse.hpp"

#include <QScreen>
#include <QTimer>
#include <QValidator>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include "cocoa_mouse.hpp"
#endif

extern "C"
{
#include <86box/86box.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/keyboard.h>
}

extern MainWindow* main_window;
RendererStack::RendererStack(QWidget *parent) :
    QStackedWidget(parent),
    ui(new Ui::RendererStack)
{
    ui->setupUi(this);

#ifdef __ANDROID__
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setInputMethodHints(inputMethodHints() | Qt::ImhLatinOnly | Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText);
    connect(QApplication::inputMethod(), &QInputMethod::visibleChanged, this, &RendererStack::keyboardVisibleChanged);
#endif

    mouse_input_backends[QApplication::platformName()].init();
}

RendererStack::~RendererStack()
{
    delete ui;
}

extern "C" void macos_poll_mouse();
void
qt_mouse_capture(int on)
{
    if (!on)
    {
        mouse_capture = 0;
        QApplication::setOverrideCursor(Qt::ArrowCursor);
        return;
    }
    mouse_capture = 1;
    QApplication::setOverrideCursor(Qt::BlankCursor);
    return;
}

void RendererStack::mousePoll()
{
    mouse_x = mousedata.deltax;
    mouse_y = mousedata.deltay;
    mouse_z = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons = mousedata.mousebuttons;
    mouse_input_backends[QApplication::platformName()].poll_mouse();
}

int ignoreNextMouseEvent = 1;
void RendererStack::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->geometry().contains(event->pos()) && event->button() == Qt::LeftButton && !mouse_capture)
    {
        plat_mouse_capture(1);
        this->setCursor(Qt::BlankCursor);
        if (!ignoreNextMouseEvent) ignoreNextMouseEvent++; // Avoid jumping cursor when moved.
        return;
    }
    if (mouse_capture && event->button() == Qt::MiddleButton && mouse_get_buttons() < 3)
    {
        plat_mouse_capture(0);
        this->setCursor(Qt::ArrowCursor);
        return;
    }
    if (mouse_capture)
    {
        mousedata.mousebuttons &= ~event->button();
    }
}
void RendererStack::mousePressEvent(QMouseEvent *event)
{
    if (mouse_capture)
    {
        mousedata.mousebuttons |= event->button();
    }
    if (main_window->frameGeometry().contains(event->pos()) && !geometry().contains(event->pos()))
    {
        main_window->windowHandle()->startSystemMove();
    }
    event->accept();
}
void RendererStack::wheelEvent(QWheelEvent *event)
{
    if (mouse_capture)
    {
        mousedata.deltaz += event->pixelDelta().y();
    }
}

void RendererStack::mouseMoveEvent(QMouseEvent *event)
{
    if (QApplication::platformName().contains("wayland"))
    {
        event->accept();
        return;
    }
    if (!mouse_capture) { event->ignore(); return; }
#ifdef __APPLE__
    event->accept();
    return;
#else
    static QPoint oldPos = QCursor::pos();
    if (ignoreNextMouseEvent) { oldPos = event->pos(); ignoreNextMouseEvent--; event->accept(); return; }
    mousedata.deltax += event->pos().x() - oldPos.x();
    mousedata.deltay += event->pos().y() - oldPos.y();
    if (QApplication::platformName() == "eglfs")
    {
        leaveEvent((QEvent*)event);
        ignoreNextMouseEvent--;
    }
    else if (event->globalPos().x() == 0 || event->globalPos().y() == 0) leaveEvent((QEvent*)event);
    else if (event->globalPos().x() == (screen()->geometry().width() - 1) || event->globalPos().y() == (screen()->geometry().height() - 1)) leaveEvent((QEvent*)event);
    oldPos = event->pos();
#endif
}

void RendererStack::leaveEvent(QEvent* event)
{
    if (QApplication::platformName().contains("wayland"))
    {
        event->accept();
        return;
    }
    if (!mouse_capture) return;
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
    ignoreNextMouseEvent = 2;
    event->accept();
}

void RendererStack::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->ignore();
}

void RendererStack::switchRenderer(Renderer renderer) {
    startblit();
    if (current) {
        removeWidget(current.get());
    }

    switch (renderer) {
    case Renderer::Software:
    {
        auto sw = new SoftwareRenderer(this);        
        rendererWindow = sw;
        connect(this, &RendererStack::blitToRenderer, sw, &SoftwareRenderer::onBlit, Qt::QueuedConnection);
#ifdef RENDERER_COMMON_USE_WIDGETS
        current.reset(sw);
#else
        current.reset(this->createWindowContainer(sw, this));
#endif
    }
        break;
    case Renderer::OpenGL:
    {
        this->createWinId();
        auto hw = new HardwareRenderer(this);
        rendererWindow = hw;
        connect(this, &RendererStack::blitToRenderer, hw, &HardwareRenderer::onBlit, Qt::QueuedConnection);
#ifdef RENDERER_COMMON_USE_WIDGETS
        current.reset(hw);
#else
        current.reset(this->createWindowContainer(hw, this));
#endif
        break;
    }
    case Renderer::OpenGLES:
    {
        this->createWinId();
        auto hw = new HardwareRenderer(this, HardwareRenderer::RenderType::OpenGLES);
        rendererWindow = hw;
        connect(this, &RendererStack::blitToRenderer, hw, &HardwareRenderer::onBlit, Qt::QueuedConnection);
#ifdef RENDERER_COMMON_USE_WIDGETS
        current.reset(hw);
#else
        current.reset(this->createWindowContainer(hw, this));
#endif
        break;
    }
    case Renderer::OpenGL3:
    {
        this->createWinId();
        auto hw = new HardwareRenderer(this, HardwareRenderer::RenderType::OpenGL3);
        rendererWindow = hw;
        connect(this, &RendererStack::blitToRenderer, hw, &HardwareRenderer::onBlit, Qt::QueuedConnection);
#ifdef RENDERER_COMMON_USE_WIDGETS
        current.reset(hw);
#else
        current.reset(this->createWindowContainer(sw, this));
#endif
        break;
    }
    }

    imagebufs = std::move(rendererWindow->getBuffers());

    current->setFocusPolicy(Qt::NoFocus);
    current->setFocusProxy(this);
#ifdef __ANDROID__
    current->setAttribute(Qt::WA_AcceptTouchEvents, true);
#endif
    addWidget(current.get());

    this->setStyleSheet("background-color: black");

    endblit();
}

// called from blitter thread
void RendererStack::blit(int x, int y, int w, int h)
{
    if ((w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL) || std::get<std::atomic_flag*>(imagebufs[currentBuf])->test_and_set())
    {
        video_blit_complete();
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h = h;
    uint8_t* imagebits = std::get<uint8_t*>(imagebufs[currentBuf]);
    for (int y1 = y; y1 < (y + h - 1); y1++)
    {
        auto scanline = imagebits + (y1 * (2048) * 4) + (x * 4);
        video_copy(scanline, &(buffer32->line[y1][x]), w * 4);
    }

    if (screenshots)
    {
        video_screenshot((uint32_t *)imagebits, x, y, 2048);
    }
    video_blit_complete();
    emit blitToRenderer(currentBuf, sx, sy, sw, sh);
    currentBuf = (currentBuf + 1) % imagebufs.size();
}

bool RendererStack::event(QEvent* event)
{
    switch (event->type()) {
#ifdef __ANDROID__
        case QEvent::TouchBegin:
            touchInProgress = true;
            touchUpdated = false;
            touchTap++;
            if (touchTap >= 2)
            {
                touchUpdated = true;
                mousedata.mousebuttons |= 1;
            }
            QTimer::singleShot(QApplication::doubleClickInterval(), this, [this]
            {
                if (touchTap) touchTap--;
            });
            break;
        case QEvent::TouchUpdate:
            {
                touchUpdated = true;
                auto touchevent = ((QTouchEvent*)(event));
                for (int i = 0; i < touchevent->points().size(); i++)
                {
                    mousedata.deltax += (touchevent->points()[i].position() - touchevent->points()[i].lastPosition()).x();
                    mousedata.deltay += (touchevent->points()[i].position() - touchevent->points()[i].lastPosition()).y();
                }
                break;
            }
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            touchInProgress = false;
            if (mousedata.mousebuttons & 1)
            {
                mousedata.mousebuttons &= ~1;
            }
            else if (!touchUpdated)
            {
                mousedata.mousebuttons |= 1;
                QTimer::singleShot(100, this, [this]
                {
                    if (!touchInProgress) mousedata.mousebuttons &= ~1;
                });
            }
            touchUpdated = false;
            break;
#endif
         default:
            break;
    }
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchUpdate || event->type() == QEvent::TouchCancel)
    {
        event->setAccepted(cpu_thread_run == 1);
        return true;
    }
    return QStackedWidget::event(event);
}

void RendererStack::keyboardVisibleChanged()
{
    this->imeInputText = "";
}

void RendererStack::inputMethodEvent(QInputMethodEvent* event)
{
    if (event->attributes().length() == 0) return;
    if (event->preeditString().length() == 0) return;
    static const QMap<QChar, std::array<uint32_t, 2>> latinToXt =
    {
        {'\x1b', {0x01}},
        {'1', {0x02}},
        {'!', {0x02, 1}},
        {'2', {0x03}},
        {'@', {0x03, 1}},
        {'3', {0x04}},
        {'#', {0x04, 1}},
        {'4', {0x05}},
        {'$', {0x05, 1}},
        {'5', {0x06}},
        {'%', {0x06, 1}},
        {'6', {0x07}},
        {'^', {0x07, 1}},
        {'7', {0x08}},
        {'&', {0x08, 1}},
        {'8', {0x09}},
        {'9', {0x0A}},
        {'(', {0x0A, 1}},
        {'0', {0x0B}},
        {')', {0x0B, 1}},
        {'-', {0x4A}},
        {'_', {0x0C, 1}},
        {'=', {0x0D}},
        {'+', {0x4E}},
        {'\t', {0x0F}},
        {'q', {0x10}},
        {'w', {0x11}},
        {'e', {0x12}},
        {'r', {0x13}},
        {'t', {0x14}},
        {'y', {0x15}},
        {'u', {0x16}},
        {'i', {0x17}},
        {'o', {0x18}},
        {'p', {0x19}},
        {'{', {0x1A, 1}},
        {'[', {0x1A}},
        {'}', {0x1B, 1}},
        {']', {0x1B}},
        {'\n', {0x1C}},
        {'a', {0x1E}},
        {'s', {0x1F}},
        {'d', {0x20}},
        {'f', {0x21}},
        {'g', {0x22}},
        {'h', {0x23}},
        {'j', {0x24}},
        {'k', {0x25}},
        {'l', {0x26}},
        {';', {0x27}},
        {':', {0x27, 1}},
        {'\'', {0x28}},
        {'"', {0x28, 1}},
        {'`', {0x29}},
        {'~', {0x29, 1}},
        {'\\', {0x2B}},
        {'|', {0x2B, 1}},
        {'z', {0x2C}},
        {'x', {0x2D}},
        {'c', {0x2E}},
        {'v', {0x2F}},
        {'b', {0x30}},
        {'n', {0x31}},
        {'m', {0x32}},
        {',', {0x33}},
        {'<', {0x33, 1}},
        {'.', {0x34}},
        {'>', {0x34, 1}},
        {'/', {0x35}},
        {'?', {0x35, 1}},
        {'*', {0x37}},
        {' ', {0x39}},
    };
    if (event->preeditString()[0] <= (QChar)127 && event->preeditString()[0].isLetter() && event->preeditString()[0].isUpper())
    {
        QChar lowercaseCharacter = event->preeditString()[0].toLower();
        keyboard_input(1, 0x2A);
        keyboard_input(1, latinToXt[lowercaseCharacter][0]);
        keyboard_input(0, latinToXt[lowercaseCharacter][0]);
        keyboard_input(0, 0x2A);
    }
    else
    {
        bool shiftNeeded = latinToXt[event->preeditString()[0]][1];
        if (shiftNeeded) keyboard_input(1, 0x2A);
        keyboard_input(1, latinToXt[event->preeditString()[0]][0]);
        keyboard_input(0, latinToXt[event->preeditString()[0]][0]);
        if (shiftNeeded) keyboard_input(0, 0x2A);
    }
    event->accept();
}
