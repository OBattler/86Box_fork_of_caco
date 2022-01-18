#ifdef EVDEV_INPUT
class QWindow;
void evdev_init();
void evdev_mouse_capture(QWindow*);
void evdev_mouse_uncapture();
void evdev_mouse_poll();
#endif
