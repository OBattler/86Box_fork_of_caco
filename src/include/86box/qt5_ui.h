#include <QApplication>
#include <QTimer>
#include <QWindow>
#include <QThread>
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

class FileDlgClass : public QObject
{
	Q_OBJECT
public slots:
	void filedialog(FileOpenSaveRequest);
};

class SDLThread : public QThread
{
    Q_OBJECT

public:
	SDLThread(int argc, char** argv);
signals:
    void fileopendialog(FileOpenSaveRequest);
protected:
    void run() override;
private:
	int sdl_main(int argc, char** argv);
	int pass_argc;
	char** pass_argv;
};