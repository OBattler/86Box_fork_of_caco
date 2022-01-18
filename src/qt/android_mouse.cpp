#include "android_mouse.hpp"
#include <jni.h>
#include <atomic>

static std::atomic<float> android_mouse_relx, android_mouse_rely;

#include <QJniObject>
#include <QApplication>

extern "C"
{
#include <86box/plat.h>
#include <86box/mouse.h>
}

void android_init() {}

void android_mouse_capture(QWindow* window)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]()
    {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        activity.callMethod<void>("captureMouse");
    }).waitForFinished();
}

void android_mouse_uncapture()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]()
    {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        activity.callMethod<void>("uncaptureMouse");
    }).waitForFinished();
}

void android_mouse_poll()
{
    mouse_x = android_mouse_relx;
    mouse_y = android_mouse_rely;
    android_mouse_relx = 0;
    android_mouse_rely = 0;
}

extern "C"
{
JNIEXPORT void JNICALL Java_src_android_src_net_eightsixbox_eightsixbox_EmuActivity_onMouseMoveEvent(JNIEnv* env, jobject obj, jfloat relx, jfloat rely)
{
    android_mouse_relx = android_mouse_relx + relx;
    android_mouse_rely = android_mouse_rely + rely;
}
}
