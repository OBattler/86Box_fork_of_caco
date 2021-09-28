#ifdef __linux__
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1
#endif

#include <SDL.h>
#include <86box/unix.h>
#include <QFileDialog>
#include <QLayout>

#include <SDL_syswm.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <dlfcn.h>
#include <wchar.h>

#include <thread>

#include <86box/language.h>
extern "C"
{
#include <86box/86box.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/device.h>
#include <86box/unix_sdl.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/ui.h>
#include <86box/video.h>
}
#include <86box/gameport.h>

extern "C" bool     ImGui_ImplSDL2_Init(SDL_Window* window);
extern "C" void     ImGui_ImplSDL2_Shutdown();
extern "C" void     ImGui_ImplSDL2_NewFrame();
extern "C" bool     ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event);
uint64_t	StartingTime;
uint64_t Frequency;
int rctrl_is_lalt;
int	update_icons = 1;
int	kbd_req_capture;
int hide_status_bar;
int fixed_size_x = 640;
int fixed_size_y = 480;
extern int title_set;
extern wchar_t sdl_win_title[512];
plat_joystick_t	plat_joystick_state[MAX_PLAT_JOYSTICKS];
joystick_t	joystick_state[MAX_JOYSTICKS];
int		joysticks_present;
SDL_mutex *blitmtx;
SDL_threadID eventthread;
static int exit_event = 0;
int fullscreen_pending = 0;
extern float menubarheight;
QWindow* sdlwindow = nullptr;

extern const uint16_t sdl_to_xt[0x200];

typedef struct sdl_blit_params
{
    int x, y, w, h;
} sdl_blit_params;

sdl_blit_params params = { 0, 0, 0, 0 };
int blitreq = 0;
int limitedblitreq = 0;

wchar_t* plat_get_string_real(uintptr_t i)
{
    switch (i)
    {
        case IDS_2077:
            return L"Click to capture mouse.";
        case IDS_2078:
            return L"Press CTRL-END to release mouse";
        case IDS_2079:
            return L"Press CTRL-END or middle button to release mouse";
        case IDS_2080:
            return L"Failed to initialize FluidSynth";
        case IDS_4099:
            return L"MFM/RLL or ESDI CD-ROM drives never existed";
        case IDS_2093:
            return L"Failed to set up PCap";
        case IDS_2094:
            return L"No PCap devices found";
        case IDS_2110:
            return L"Unable to initialize FreeType";
        case IDS_2111:
            return L"Unable to initialize SDL, libsdl2 is required";
        case IDS_2131:
            return L"libfreetype is required for ESC/P printer emulation.";
        case IDS_2132:
            return L"libgs is required for automatic conversion of PostScript files to PDF.\n\nAny documents sent to the generic PostScript printer will be saved as PostScript (.ps) files.";
        case IDS_2129:
            return L"Make sure libpcap is installed and that you are on a libpcap-compatible network connection.";
        case IDS_2114:
            return L"Unable to initialize Ghostscript";
        case IDS_2142:
            return L"Monitor in sleep mode";
    }
    return L"";
}

extern "C" wchar_t* plat_get_string(int id)
{
    return plat_get_string_real(id);
}

void
ui_sb_update_tip(int arg)
{

}

volatile int cpu_thread_run = 1;

void
main_thread(void *param)
{
    uint32_t old_time, new_time;
    int drawits, frames;

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    framecountx = 0;
    //title_update = 1;
    old_time = SDL_GetTicks();
    drawits = frames = 0;
    while (!is_quit && cpu_thread_run) {
	/* See if it is time to run a frame of code. */
	new_time = SDL_GetTicks();
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
			nvr_save();
			nvr_dosave = 0;
			frames = 0;
		}
	} else	/* Just so we dont overload the host OS. */
		SDL_Delay(1);

	/* If needed, handle a screen resize. */
	if (doresize && !video_fullscreen && !is_quit) {
		if (vid_resize & 2)
			plat_resize(fixed_size_x, fixed_size_y);
		else
			plat_resize(scrnsz_x, scrnsz_y);
		doresize = 0;
	}
    }

    is_quit = 1;
}

thread_t* thMain = NULL;

void
do_start(void)
{
    /* We have not stopped yet. */
    is_quit = 0;

    /* Initialize the high-precision timer. */
    SDL_InitSubSystem(SDL_INIT_TIMER);
    timer_freq = SDL_GetPerformanceFrequency();

    /* Start the emulator, really. */
    thMain = thread_create(main_thread, NULL);
}

extern "C" void video_blit_complete();

void
do_stop(void)
{
    if (SDL_ThreadID() != eventthread)
    {
        exit_event = 1;
        return;
    }
    if (blitreq)
    {
        blitreq = 0;
        video_blit_complete();
    }

    while(SDL_TryLockMutex(blitmtx) == SDL_MUTEX_TIMEDOUT)
    {
        if (blitreq)
        {
            blitreq = 0;
            video_blit_complete();
        }
    }
    startblit();

    is_quit = 1;
    sdl_close();

    pc_close(thMain);

    thMain = NULL;
}

int	ui_msgbox(int flags, void *message)
{
    return ui_msgbox_header(flags, message, NULL);
}

int	ui_msgbox_header(int flags, void *message, void* header)
{
    if (!header) header = (void*)L"86Box";
    if (flags & MBX_ANSI)
    {
        fwprintf(stderr, L"%s\n", header);
        fprintf(stderr, "==========================\n"
            "%s\n", message);
        return 0;
    }
    fwprintf(stderr, L"%s\n", header);
    fwprintf(stderr, L"==========================\n"
    L"%s\n", plat_get_string_real((uintptr_t)message));
    return 0;
}

void
plat_power_off(void)
{
    confirm_exit = 0;
    nvr_save();
    config_save();

    /* Deduct a sufficiently large number of cycles that no instructions will
       run before the main thread is terminated */
    cycles -= 99999999;

    cpu_thread_run = 0;
}

extern "C" void     sdl_blit(int x, int y, int w, int h);

typedef struct mouseinputdata
{
    int deltax, deltay, deltaz;
    int mousebuttons;
} mouseinputdata;
SDL_mutex* mousemutex;
static mouseinputdata mousedata;
void mouse_poll()
{
    SDL_LockMutex(mousemutex);
    mouse_x = mousedata.deltax;
    mouse_y = mousedata.deltay;
    mouse_z = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons = mousedata.mousebuttons;
    SDL_UnlockMutex(mousemutex);
}

void ui_sb_set_ready(int ready) {}

uint32_t timer_onesec(uint32_t interval, void* param)
{
        pc_onesec();
        return interval;
}

extern "C"
{
extern void InitImGui();
extern bool ImGuiWantsMouseCapture();
extern bool ImGuiWantsKeyboardCapture();
extern bool IsFileDlgOpen();
extern void sdl_real_blit(SDL_Rect* r_src);
extern void ui_window_title_real();
extern void sdl_reinit_texture();
}

extern SDL_Window* sdl_win;
#if 0
static FileDlgClass* fdlg;
void FileDlgClass::filedialog(FileOpenSaveRequest req)
{
    QString finalfilterstr;
    if (sdlwindow)
    {
        for (auto& curfilter : req.filters)
        {
            finalfilterstr.append(std::get<0>(curfilter).c_str());
            finalfilterstr.append(";;");
        }
        finalfilterstr.resize(finalfilterstr.size() - 2);
        finalfilterstr.push_back(QChar(0));

    }
}
#endif
static EmuMainWindow* mainwnd;
SDLThread::SDLThread(int argc, char** argv)
: QThread(nullptr)
{
    pass_argc = argc;
    pass_argv = argv;
}
SDLThread::~SDLThread()
{

}
void SDLThread::run()
{
    //connect(this, SIGNAL(fileopendialog(FileOpenSaveRequest)), fdlg, SLOT(filedialog(FileOpenSaveRequest)));
    exit(sdl_main(pass_argc, pass_argv));
}
int SDLThread::sdl_main(int argc, char** argv)
{
    SDL_Event event;

    eventthread = SDL_ThreadID();
    blitmtx = SDL_CreateMutex();
    if (!blitmtx)
    {
        fprintf(stderr, "Failed to create blit mutex: %s", SDL_GetError());
        exit(-1);
    }

    mousemutex = SDL_CreateMutex();
    SDL_LockMutex(mousemutex);
    int initok = 0;
    switch (vid_api)
    {
        case 0:
            initok = sdl_inits();
            break;
        default:
        case 1:
            initok = sdl_inith();
            break;
        case 2:
            initok = sdl_initho();
            break;
    }
    if (initok == -1) { fprintf(stderr, "SDL init failed"); exit(-1); }
    SDL_UnlockMutex(mousemutex);

    if (start_in_fullscreen)
    {
        video_fullscreen = 1;
	    sdl_set_fs(1);
    }
    /* Fire up the machine. */
    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    //plat_pause(0);

    /* Initialize the rendering window, or fullscreen. */

    do_start();

    SDL_AddTimer(1000, timer_onesec, NULL);
    //InitImGui();
    while (!is_quit)
    {
        static int mouse_inside = 0;
        while (SDL_PollEvent(&event))
	    {
            //if (!mouse_capture) ImGui_ImplSDL2_ProcessEvent(&event);
            switch(event.type)
            {
                case SDL_QUIT:
                {
                    #if 0
                    if (IsFileDlgOpen())
                    {
                        int curdopause = dopause;
                        plat_pause(1);
                        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "86Box",
                                    "A file dialog is open. Please close it before exiting 86Box.\n", NULL);
                        plat_pause(curdopause);
                        break;
                    }
                    #endif
                	exit_event = 1;
                	break;
                }
                case SDL_MOUSEWHEEL:
                {
                    if (mouse_capture)
                    {
                        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                        {
                            event.wheel.x *= -1;
                            event.wheel.y *= -1;
                        }
                        SDL_LockMutex(mousemutex);
                        mousedata.deltaz = event.wheel.y;
                        SDL_UnlockMutex(mousemutex);
                    }
                    break;
                }
                case SDL_MOUSEMOTION:
                {
                    if (ImGuiWantsMouseCapture()) break;
                    if (mouse_capture)
                    {
                        SDL_LockMutex(mousemutex);
                        mousedata.deltax += event.motion.xrel;
                        mousedata.deltay += event.motion.yrel;
                        SDL_UnlockMutex(mousemutex);
                    }
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                {
                    if ((event.button.button == SDL_BUTTON_LEFT)
                    && !(mouse_capture)
                    && event.button.state == SDL_RELEASED
                    && (mouse_inside || video_fullscreen))
                    {
                        plat_mouse_capture(1);
                        break;
                    }
                    if (mouse_get_buttons() < 3 && event.button.button == SDL_BUTTON_MIDDLE)
                    {
                        plat_mouse_capture(0);
                        break;
                    }
                    if (mouse_capture)
                    {
                        int buttonmask = 0;

                        switch(event.button.button)
                        {
                            case SDL_BUTTON_LEFT:
                                buttonmask = 1;
                                break;
                            case SDL_BUTTON_RIGHT:
                                buttonmask = 2;
                                break;
                            case SDL_BUTTON_MIDDLE:
                                buttonmask = 4;
                                break;
                        }
                        SDL_LockMutex(mousemutex);
                        if (event.button.state == SDL_PRESSED)
                        {
                            mousedata.mousebuttons |= buttonmask;
                        }
                        else mousedata.mousebuttons &= ~buttonmask;
                        SDL_UnlockMutex(mousemutex);
                    }
                    break;
                }
                case SDL_RENDER_DEVICE_RESET:
                case SDL_RENDER_TARGETS_RESET:
                    {    
                        extern void sdl_reinit_texture();
                        sdl_reinit_texture();
                        break;
                    }
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                {
                    if (kbd_req_capture && !mouse_capture) break;
                    uint16_t xtkey = 0;
                    switch(event.key.keysym.scancode)
                    {
                        default:
                            xtkey = sdl_to_xt[event.key.keysym.scancode];
                            break;
                    }
                    if ((xtkey == 0x11D) && rctrl_is_lalt)
			            xtkey = 0x038;
                    keyboard_input(event.key.state == SDL_PRESSED, xtkey);
                }
                case SDL_WINDOWEVENT:
                {
                    switch (event.window.event)
                    {
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                    {
                        window_w = event.window.data1;
                        window_h = event.window.data2;
                        if (window_remember) config_save();
                        break;
                    }
                    case SDL_WINDOWEVENT_MOVED:
                    {
                        if (strncasecmp(SDL_GetCurrentVideoDriver(), "wayland", 7) != 0)
                        {
                            window_x = event.window.data1;
                            window_y = event.window.data2;
                            if (window_remember) config_save();
                        }
                        break;
                    }
                    case SDL_WINDOWEVENT_ENTER:
                        mouse_inside = 1;
                        break;
                    case SDL_WINDOWEVENT_LEAVE:
                        mouse_inside = 0;
                        break;
                    }
                }
            }
	    }
        if (mouse_capture && keyboard_ismsexit())
        {
            plat_mouse_capture(0);
        }
        if (blitreq)
        {
            extern void sdl_blit(int x, int y, int w, int h);
            sdl_blit(params.x, params.y, params.w, params.h);
        }
        else
        {
            SDL_Rect srcrect;
            memcpy(&srcrect, &params, sizeof(SDL_Rect));
            sdl_real_blit(&srcrect);
        }
        if (title_set)
        {
            extern void ui_window_title_real();
            ui_window_title_real();
        }
        if (video_fullscreen && keyboard_isfsexit())
        {
            sdl_set_fs(0);
            video_fullscreen = 0;
        }
        if (!(video_fullscreen) && keyboard_isfsenter())
        {
            sdl_set_fs(1);
            video_fullscreen = 1;
        }
        if (fullscreen_pending)
        {
            sdl_set_fs(video_fullscreen);
            fullscreen_pending = 0;
        }
        if ((keyboard_recv(0x1D) || keyboard_recv(0x11D)) && keyboard_recv(0x58))
        {
            pc_send_cad();
        }
        if ((keyboard_recv(0x1D) || keyboard_recv(0x11D)) && keyboard_recv(0x57))
        {
            take_screenshot();
        }
        if (exit_event)
        {
            do_stop();
            break;
        }
    }
    printf("\n");
    SDL_DestroyMutex(blitmtx);
    SDL_DestroyMutex(mousemutex);
    SDL_Quit();
    QApplication::quit();
    return 0;
}

void EmuRenderWindow::exposeEvent(QExposeEvent *event)
{
    if (isExposed())
    {
        renderNow();
    }
}

EmuRenderWindow::EmuRenderWindow(QWindow* parent)
: QRasterWindow(parent), m_backingStore(new QBackingStore(this)), m_image(2048, 2048, QImage::Format_ARGB32)
{
    setGeometry(0, 0, 640, 480);
}

void EmuRenderWindow::qt_real_blit(int x, int y, int w, int h)
{
    //printf("Offpainter thread ID: %X\n", SDL_ThreadID());
    if ((w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL))
    {
        video_blit_complete();
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h = h;
    QPainter painteronImage(&m_image);
    painteronImage.drawImage(QRect(x, y, w, h), QImage((uchar*)&(buffer32->line[y][x]), 2048, h, (2048 + 64) * 4, QImage::Format_RGB32));
    video_blit_complete();
    painteronImage.end();
    renderNow();  
}

void EmuRenderWindow::renderNow()
{
    if (!isExposed())
        return;

    QRect rect(0, 0, width(), height());
    m_backingStore->beginPaint(rect);

    QPaintDevice *device = m_backingStore->paintDevice();
    QPainter painter(device);
    painter.drawImage(QRect(0, 0, w, h), m_image, QRect(sx, sy, sw, sh));
    //painter.fillRect(rect, Qt::GlobalColor::red);
    painter.end();
    m_backingStore->endPaint();
    m_backingStore->flush(rect);
    requestUpdate();
}

bool EmuRenderWindow::event(QEvent* event)
{
    if (event->type() == QEvent::UpdateRequest) { renderNow(); return true; }
    return QWindow::event(event);
}

void EmuRenderWindow::renderLater()
{
    requestUpdate();
}

void EmuRenderWindow::resizeEvent(QResizeEvent* evnt)
{
    m_backingStore->resize(evnt->size());
}

EmuMainWindow::EmuMainWindow(QWidget* parent)
: QMainWindow(parent)
{
    setGeometry(0, 0, 640, 480);
    setWindowTitle("86Box");

    this->child = new EmuRenderWindow(this->windowHandle());
    this->childContainer = createWindowContainer(this->child, this);
    this->setCentralWidget(childContainer);
    connect(this, SIGNAL(qt_blit(int, int, int, int)), this->child, SLOT(qt_real_blit(int, int, int, int)));
}

void qt5_blit(int x, int y, int w, int h)
{
    emit mainwnd->qt_blit(x, y, w, h);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    pc_init(argc, argv);
    if (! pc_init_modules()) {
        fprintf(stderr, "No ROMs found.\n");
        return 6;
    }

#ifdef __unix__
    if (app.platformName() == "xcb") setenv("SDL_VIDEODRIVER", "x11", 1);
    else if (app.platformName().contains("wayland")) setenv("SDL_VIDEODRIVER", "wayland", 1);
#endif
    SDL_Init(0);
    printf("Main thread ID: 0x%X\n", SDL_ThreadID());
    blitmtx = SDL_CreateMutex();
    
    mousemutex = SDL_CreateMutex();
#if 0
    SDLThread sdlthr(argc, argv);
    sdlthr.start();
    while(1)
    {
        SDL_LockMutex(mousemutex);
        if (sdl_win != NULL) break;
        SDL_UnlockMutex(mousemutex);
    }
    SDL_SysWMinfo wminfo;
    SDL_VERSION(&wminfo.version);
    SDL_GetWindowWMInfo(sdl_win, &wminfo);
    WId windowid;
#ifdef __APPLE__
    windowid = wminfo.info.cocoa.window;
#else
    if (wminfo.subsystem == SDL_SYSWM_X11)
    {
        windowid = wminfo.info.x11.window;
    }
#endif
#endif
    mainwnd = new EmuMainWindow(nullptr);
    video_setblit(qt5_blit);
    mainwnd->show();
    pc_reset_hard_init();
    do_start();
    app.exec();
    delete mainwnd;
    SDL_Quit();
    return 0;
}
char* plat_vidapi_name(int i)
{
    switch (i)
    {
        case 0:
            return "sdl_software";
        case 1:
        default:
            return "default";
        case 2:
            return "sdl_opengl";
    }
}
void joystick_init(void) {}
void joystick_close(void) {}
void joystick_process(void) {}
void startblit()
{
    SDL_LockMutex(blitmtx);
}

void endblit()
{
    SDL_UnlockMutex(blitmtx);
}
