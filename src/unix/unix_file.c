#ifdef __linux__
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1
#endif
#include <SDL.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <dlfcn.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/unix_sdl.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/ui.h>
void* dynld_module(const char *name, dllimp_t *table)
{
    dllimp_t* imp;
    void* modhandle = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
    if (modhandle)
    {
        for (imp = table; imp->name != NULL; imp++)
        {
            if ((*(void**)imp->func = dlsym(modhandle, imp->name)) == NULL)
            {
                dlclose(modhandle);
                return NULL;
            }
        }
    }
    return modhandle;
}

void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    struct tm* calendertime;
    struct timeval t;
    time_t curtime;

    if (prefix != NULL)
	sprintf(bufp, "%s-", prefix);
      else
	strcpy(bufp, "");
    gettimeofday(&t, NULL);
    curtime = time(NULL);
    calendertime = localtime(&curtime);
    sprintf(&bufp[strlen(bufp)], "%d%02d%02d-%02d%02d%02d-%03ld%s", calendertime->tm_year, calendertime->tm_mon, calendertime->tm_mday, calendertime->tm_hour, calendertime->tm_min, calendertime->tm_sec, t.tv_usec / 1000, suffix);
}

int
plat_getcwd(char *bufp, int max)
{
    return getcwd(bufp, max) != 0;
}

int
plat_chdir(char* str)
{
    return chdir(str);
}

void dynld_close(void *handle)
{
	dlclose(handle);
}

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

FILE *
plat_fopen64(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int
plat_path_abs(char *path)
{
    return path[0] == '/';
}

void
plat_path_slash(char *path)
{
    if ((path[strlen(path)-1] != '/')) {
	strcat(path, "/");
    }
}

void
plat_put_backslash(char *s)
{
    int c = strlen(s) - 1;

    if (s[c] != '/')
	   s[c] = '/';
}

/* Return the last element of a pathname. */
char *
plat_get_basename(const char *path)
{
    int c = (int)strlen(path);

    while (c > 0) {
	if (path[c] == '/')
	   return((char *)&path[c + 1]);
       c--;
    }

    return((char *)path);
}
char *
plat_get_filename(char *s)
{
    int c = strlen(s) - 1;

    while (c > 0) {
	if (s[c] == '/' || s[c] == '\\')
	   return(&s[c+1]);
       c--;
    }

    return(s);
}


char *
plat_get_extension(char *s)
{
    int c = strlen(s) - 1;

    if (c <= 0)
	return(s);

    while (c && s[c] != '.')
		c--;

    if (!c)
	return(&s[strlen(s)]);

    return(&s[c+1]);
}


void
plat_append_filename(char *dest, const char *s1, const char *s2)
{
    strcpy(dest, s1);
    plat_path_slash(dest);
    strcat(dest, s2);
}

int
plat_dir_check(char *path)
{
    struct stat dummy;
    if (stat(path, &dummy) < 0)
    {
        return 0;
    }
    return S_ISDIR(dummy.st_mode);
}

int
plat_dir_create(char *path)
{
    return mkdir(path, S_IRWXU);
}

void *
plat_mmap(size_t size, uint8_t executable)
{
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE, 0, 0);
    return (ret < 0) ? NULL : ret;
}

void
plat_munmap(void *ptr, size_t size)
{
    munmap(ptr, size);
}

void plat_remove(char* path)
{
    remove(path);
}

void
plat_get_dirname(char *dest, const char *path)
{
    int c = (int)strlen(path);
    char *ptr;

    ptr = (char *)path;

    while (c > 0) {
	if (path[c] == '/' || path[c] == '\\') {
		ptr = (char *)&path[c];
		break;
	}
 	c--;
    }

    /* Copy to destination. */
    while (path < ptr)
	*dest++ = *path++;
    *dest = '\0';
}

int stricmp(const char* s1, const char* s2)
{
    return strcasecmp(s1, s2);
}

int strnicmp(const char *s1, const char *s2, size_t n)
{
    return strncasecmp(s1, s2, n);
}

void plat_get_exe_name(char *s, int size)
{
    char* basepath = SDL_GetBasePath();
    snprintf(s, size, "%s%s", basepath, basepath[strlen(basepath) - 1] == '/' ? "86box" : "/86box");
}
