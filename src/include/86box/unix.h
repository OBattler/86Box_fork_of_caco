#include <QApplication>
#include <QTimer>
#include <QWindow>
#include <QThread>
#include <QMainWindow>
#include <QRasterWindow>
#include <QSizePolicy>
#include <QBackingStore>
#include <QEvent>
#include <QResizeEvent>
#include <QPainter>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <atomic>
#include <array>
#include <thread>

static std::vector<std::pair<std::string, std::string>> allfilefilter
{
#ifdef _WIN32
	{"All Files", "*.*"}
#else
    {"All Files", "*"}
#endif
};

struct FileOpenSaveRequest
{
	std::vector<std::pair<std::string, std::string>>& filters = allfilefilter;
	void (*filefunc3params)(uint8_t, char*, uint8_t) = nullptr;
	void (*filefunc2params)(uint8_t, char*) = nullptr;
	void (*filefunc2paramsalt)(char*, uint8_t) = nullptr;
	bool save = false;
	bool wp = false;
	uint8_t id = 0;
};

class SDLThread : public QThread
{
    Q_OBJECT

public:
	SDLThread(int argc, char** argv);
	virtual ~SDLThread();
//signals:
//    void fileopendialog(FileOpenSaveRequest);
protected:
    void run() override;
private:
	int sdl_main(int argc, char** argv);
	int pass_argc;
	char** pass_argv;
};

class EmuRenderWindow : public QRasterWindow
{
    Q_OBJECT

public:
    EmuRenderWindow(QWindow* parent = nullptr);
    virtual void render(QPainter *painter) { painter->drawText(0, 0, "hello"); };

public slots:
    void renderNow();
    void renderLater();
	void qt_real_blit(int x, int y, int w, int h);

protected:
    bool event(QEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;
    void exposeEvent(QExposeEvent *event) override;

private:
    QBackingStore *m_backingStore;
	QImage m_image;
	int w, h, sx, sy, sw, sh;
};

class EmuMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	EmuMainWindow(QWidget* parent = nullptr);
//public slots:
//	void resizeReq(int width, int height);
	EmuRenderWindow* child;
public slots:
	void resizeSlot(int w, int h);
	void windowTitleReal(const wchar_t* str);
signals:
	void qt_blit(int x, int y, int w, int h);
	void resizeSig(int w, int h);
	void windowTitleSig(const wchar_t* str);

private:
	QWidget* childContainer;

protected:
	void resizeEvent(QResizeEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;
};
