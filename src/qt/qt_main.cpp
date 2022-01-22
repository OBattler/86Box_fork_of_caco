#include <QApplication>
#include <QSurfaceFormat>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QDirIterator>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QFileDialog>
#include <QLayout>
#include <QPushButton>
#include <QProgressDialog>
#include <QAbstractScrollArea>
#include <QScroller>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef __ANDROID__
#include <QJniObject>
#include <private/qandroidextras_p.h>
#include <android/log.h>
#endif

#ifdef QT_STATIC
/* Static builds need plugin imports */
#include <QtPlugin>
Q_IMPORT_PLUGIN(QICOPlugin)
#ifdef Q_OS_WINDOWS
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin)
#endif
#endif

#ifdef Q_OS_WINDOWS
#include "qt_winrawinputfilter.hpp"
#endif

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/discord.h>

#include <thread>
#include <iostream>

#include "qt_mainwindow.hpp"
#include "qt_progsettings.hpp"
#include "cocoa_mouse.hpp"
#include "qt_styleoverride.hpp"

// Void Cast
#define VC(x) const_cast<wchar_t*>(x)

extern QElapsedTimer elapsed_timer;
extern MainWindow* main_window;

extern "C" {
#include <86box/timer.h>
#include <86box/nvr.h>
    extern int qt_nvr_save(void);
}

void qt_set_sequence_auto_mnemonic(bool b);

void
main_thread_fn()
{
    uint64_t old_time, new_time;
    int drawits, frames;

    QThread::currentThread()->setPriority(QThread::HighestPriority);
    framecountx = 0;
    //title_update = 1;
    old_time = elapsed_timer.elapsed();
    drawits = frames = 0;
    while (!is_quit && cpu_thread_run) {
        /* See if it is time to run a frame of code. */
        new_time = elapsed_timer.elapsed();
        drawits += (new_time - old_time);
        old_time = new_time;
        if (drawits > 0 && !dopause) {
            /* Yes, so do one frame now. */
            drawits -= 10;
            if (drawits > 50)
                drawits = 0;

            /* Run a block of code. */
            pc_run();

            /* Every 200 frames we save the machine status. */
            if (++frames >= 200 && nvr_dosave) {
                qt_nvr_save();
                nvr_dosave = 0;
                frames = 0;
            }
        } else {
            /* Just so we dont overload the host OS. */
            if (drawits < -1)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else
                std::this_thread::yield();
        }

        /* If needed, handle a screen resize. */
        if (!atomic_flag_test_and_set(&doresize) && !video_fullscreen && !is_quit) {
            if (vid_resize & 2)
                plat_resize(fixed_size_x, fixed_size_y);
            else
                plat_resize(scrnsz_x, scrnsz_y);
        }
    }

    is_quit = 1;
}

class AndroidFilter : public QObject
{
public:
    AndroidFilter(QObject* parent = nullptr)
        : QObject(parent) {}
    bool eventFilter(QObject* obj, QEvent* event)
    {
        if (qobject_cast<QAbstractScrollArea*>(obj) && event->type() == QEvent::Show)
        {
            QScroller::grabGesture(obj, QScroller::LeftMouseButtonGesture);
        }
        if (qobject_cast<QDialog*>(obj) && !qobject_cast<MainWindow*>(obj) && event->type() == QEvent::Show)
        {
            auto dialog = qobject_cast<QDialog*>(obj);
            dialog->setGeometry(main_window->geometry());
            if (dialog->maximumSize().width() < main_window->size().width()
                && dialog->maximumSize().height() < main_window->size().height())
                dialog->setFixedSize(main_window->size());
            else qobject_cast<QDialog*>(obj)->setMaximumSize(main_window->size());
            plat_mouse_capture(0);
        }
        return false;
    }
};

static QFile* logfile = nullptr;
static QTextStream* logtextfile = nullptr;
static QtMessageHandler prevHandler;
static int logfd = -1;
void EmuMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    QString txt;
    switch (type) {
    case QtInfoMsg:
        txt = QString("Info: %1").arg(msg);
        break;
    case QtDebugMsg:
        txt = QString("Debug: %1").arg(msg);
        break;
    case QtWarningMsg:
        txt = QString("Warning: %1").arg(msg);
        break;
    case QtCriticalMsg:
        txt = QString("Critical: %1").arg(msg);
        break;
    case QtFatalMsg:
        txt = QString("Fatal: %1").arg(msg);
        abort();
    }
    logfile->write(txt.toUtf8() + '\n');
    logfile->flush();
}

static int pfd[2];
static QString tag;
#ifndef _WIN32
int start_logger(const char *app_name)
{
    tag = app_name;

    /* create the pipe and redirect stdout and stderr */
    pipe(pfd);
    dup2(pfd[1], 1);
    dup2(pfd[1], 2);
    auto thread = std::thread([] ()
    {
        ssize_t rdsz;
        char buf[128];
        while((rdsz = read(pfd[0], buf, sizeof buf - 1)) > 0) {
            write(logfd, buf, rdsz);
        }
    });
    thread.detach();
}
#endif

int main(int argc, char* argv[]) {
#ifdef __ANDROID__
    /* make stdout line-buffered and stderr unbuffered */
    setvbuf(stdout, 0, _IOLBF, 0);
    setvbuf(stderr, 0, _IONBF, 0);
#endif
    QApplication app(argc, argv);
    main_window = nullptr;
#ifdef __ANDROID__
    app.installEventFilter(new AndroidFilter(&app));
#endif
    qt_set_sequence_auto_mnemonic(false);
    Q_INIT_RESOURCE(qt_resources);
    Q_INIT_RESOURCE(qt_translations);
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    app.setStyle(new StyleOverride());
    QDirIterator it(":", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        qDebug() << it.next() << "\n";
    }

#ifdef __ANDROID__
    QString str;
    QtAndroidPrivate::requestPermission(QtAndroidPrivate::Storage).waitForFinished();
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([&str, &app]()
    {
        static char curpath[4096];
        if (plat_dir_check("/storage/emulated/0/Documents/86Box") || plat_dir_create("/storage/emulated/0/Documents/86Box") == 0)
        {
            str = "/storage/emulated/0/Documents/86Box";
        }
        else
        {
            QJniObject activity = QNativeInterface::QAndroidApplication::context();
            str = activity.callObjectMethod("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;")
                    .callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString();
        }
        plat_chdir(str.toUtf8().data());
        plat_getcwd(curpath, 4096);
        qDebug() << curpath;
    }).waitForFinished();
    logfile = new QFile("./86Box.log");
    logfile->open(QFile::WriteOnly);
    logfd = logfile->handle();
    prevHandler = qInstallMessageHandler(EmuMessageHandler);
#endif


#ifdef __APPLE__
    CocoaEventFilter cocoafilter;
    app.installNativeEventFilter(&cocoafilter);
#endif
    elapsed_timer.start();

    if (!pc_init(argc, argv))
    {
        return 0;
    }
#ifdef __ANDROID__
    strcpy(rom_path, (str + "/roms/").toUtf8().constData());
#endif
    if (!main_window)
    {
        ProgSettings::loadTranslators(&app);
        main_window = new MainWindow();
        main_window->show();
    }
    if (! pc_init_modules()) {
#ifdef __ANDROID__
        QMessageBox::information(main_window, "86Box", "No ROMs found. Press OK to select a folder containing the roms directory");
        main_window->hide();
        main_window->setDisabled(true);
        QFileDialog dialog(nullptr, "Import ROM files");
        dialog.setOption(QFileDialog::DontUseNativeDialog, true);
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setDirectory("/storage/emulated/0/");
        QString curDir;
        dialog.connect(&dialog, &QFileDialog::currentChanged, &app, [&app, &dialog, &curDir](const QString& file)
        {
            qDebug() << file;
            curDir = file;
            //if (!file.isEmpty()) QTimer::singleShot(400, &dialog, [&dialog, file] () { dialog.setDirectory(file); } );
        });
        auto enterButton = new QPushButton(QObject::tr("&Open"));
        QObject::connect(enterButton, &QPushButton::released, &dialog, [&dialog] { dialog.setDirectory(dialog.selectedFiles()[0]); } );
        dialog.layout()->addWidget(enterButton);
        dialog.setFixedSize(main_window->size());
        for (uint32_t i = 0; i < dialog.layout()->count(); i++)
        {
            if (dialog.layout()->itemAt(i)->widget() && qobject_cast<QAbstractScrollArea*>(dialog.layout()->itemAt(i)->widget()))
            {
                QScroller::grabGesture(dialog.layout()->itemAt(i)->widget(), QScroller::LeftMouseButtonGesture);
            }
        }
        int result = dialog.exec();
        auto copyfunc = [&dialog, &curDir, &result]()
        {
            if (result == QDialog::Accepted)
            {
                uint32_t count = 0;
                QProgressDialog progressDialog(&dialog);
                progressDialog.show();
                progressDialog.setMinimum(0);
                progressDialog.setMaximum(1);
                progressDialog.setValue(0);
                progressDialog.setLabelText("Counting files: ");
                {
                    QDirIterator dirit(curDir.isEmpty() ? dialog.directory() : curDir, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
                    while (dirit.hasNext())
                    {
                        dirit.next();
                        if (dirit.fileInfo().isDir()) continue;
                        count++;
                        progressDialog.setLabelText(QStringLiteral("Counting files: %1").arg(count));
                        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    }
                }
                progressDialog.setLabelText("Copying file");
                progressDialog.setMaximum(count);
                count = 0;
                QDirIterator dirit(curDir.isEmpty() ? dialog.directory() : curDir, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
                while (dirit.hasNext()) {
                    auto str = dirit.next();
                    if (dirit.fileInfo().isDir()) { continue; }
                    progressDialog.setLabelText("Copying file:\n" + str + QChar('\n') + QStringLiteral("to\n") + (QString(dirit.fileInfo().canonicalPath()).replace(dirit.path(), "./") + '/' + dirit.fileInfo().fileName()));
                    QDir(QString(dirit.fileInfo().canonicalPath()).replace(dirit.path(), "./") + '/').mkpath(".");
                    QFile(QString(dirit.fileInfo().canonicalPath()).replace(dirit.path(), "./") + '/' + dirit.fileInfo().fileName()).remove();
                    if (!QFile(str).copy(QString(dirit.fileInfo().canonicalPath()).replace(dirit.path(), "./") + '/' + dirit.fileInfo().fileName()))
                    {
                        QMessageBox::critical(&progressDialog, "86Box", "Unable to copy file");
                        progressDialog.cancel();
                    }
                    progressDialog.setValue(++count);
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                }
                progressDialog.close();
            }
            QApplication::exit(6);
            return 6;
        };
        main_window->show();
        QTimer::singleShot(0, main_window, copyfunc);
        app.exec();
#else
        ui_msgbox_header(MBX_FATAL, (void*)IDS_2120, (void*)IDS_2056);
#endif
        return 6;
    }
    cpu_thread_run = 1;

    discord_load();
#ifdef __ANDROID__
    app.setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    app.installEventFilter(main_window);

#ifdef Q_OS_WINDOWS
    auto rawInputFilter = WindowsRawInputFilter::Register(main_window);
    if (rawInputFilter)
    {
        app.installNativeEventFilter(rawInputFilter.get());
        QObject::disconnect(main_window, &MainWindow::pollMouse, 0, 0);
        QObject::connect(main_window, &MainWindow::pollMouse, (WindowsRawInputFilter*)rawInputFilter.get(), &WindowsRawInputFilter::mousePoll, Qt::DirectConnection);
        main_window->setSendKeyboardInput(false);
    }
#endif

    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    // plat_pause(0);
    if (settings_only) dopause = 1;
    QTimer onesec;
    QTimer discordupdate;
    QObject::connect(&onesec, &QTimer::timeout, &app, [] {
        pc_onesec();
    });
    onesec.start(1000);
    if (discord_loaded) {
        QTimer::singleShot(1000, &app, [] {
            if (enable_discord) {
                discord_init();
                discord_update_activity(dopause);
            } else
                discord_close();
        });
        QObject::connect(&discordupdate, &QTimer::timeout, &app, [] {
            discord_run_callbacks();
        });
        discordupdate.start(0);
    }

    /* Initialize the rendering window, or fullscreen. */
    auto main_thread = std::thread([] {
       main_thread_fn();
    });

    auto ret = app.exec();
    cpu_thread_run = 0;
    main_thread.join();

    return ret;
}
