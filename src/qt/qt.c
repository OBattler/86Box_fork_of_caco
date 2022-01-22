/*
 * C functionality for Qt platform, where the C equivalent is not easily
 * implemented in Qt
 */
#if !defined(_WIN32) || !defined(__clang__)
#include <strings.h>
#endif
#include <string.h>
#include <stdint.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/nvr.h>

int qt_nvr_save(void) {
    return nvr_save();
}

char  icon_set[256] = "";  /* name of the iconset to be used */

int
plat_vidapi(char* api) {
    if (!strcasecmp(api, "default") || !strcasecmp(api, "system")) {
#ifdef __ANDROID__
        return 2;
#else
        return 0;
#endif
    } else if (!strcasecmp(api, "qt_software")) {
        return 0;
    } else if (!strcasecmp(api, "qt_opengl")) {
        return 1;
    } else if (!strcasecmp(api, "qt_opengles")) {
        return 2;
    } else if (!strcasecmp(api, "qt_opengl3")) {
        return 3;
    }

    return 0;
}

char* plat_vidapi_name(int api) {
    char* name = "default";

    switch (api) {
    case 0:
        name = "qt_software";
        break;
    case 1:
        name = "qt_opengl";
        break;
    case 2:
        name = "qt_opengles";
        break;
    case 3:
        name = "qt_opengl3";
        break;
    default:
        fatal("Unknown renderer: %i\n", api);
        break;
    }

    return name;
}
