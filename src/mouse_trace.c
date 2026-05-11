#include "mouse_trace_internal.h"

#include <stdarg.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

static SDL_IOStream *g_mouse_trace_file = NULL;
static bool g_mouse_trace_initialized = false;
static bool g_mouse_trace_enabled = false;

static void mouse_trace_init(void)
{
    if (g_mouse_trace_initialized)
    {
        return;
    }
    g_mouse_trace_initialized = true;

    const char *path = SDL_getenv("SLAYER3D_MOUSE_TRACE_FILE");
    if (path == NULL || path[0] == '\0')
    {
        const char *enabled = SDL_getenv("SLAYER3D_MOUSE_TRACE");
        if (enabled == NULL || enabled[0] == '\0' || SDL_strcmp(enabled, "0") == 0)
        {
            return;
        }
        path = "slayer3d_mouse_trace.log";
    }

    g_mouse_trace_file = SDL_IOFromFile(path, "w");
    if (g_mouse_trace_file == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D mouse trace disabled: failed to open '%s': %s", path,
                    SDL_GetError());
        return;
    }

    g_mouse_trace_enabled = true;
    slayer3d_mouse_tracef("trace", "opened path='%s'", path);
}

bool slayer3d_mouse_trace_enabled(void)
{
    mouse_trace_init();
    return g_mouse_trace_enabled;
}

void slayer3d_mouse_tracef(const char *scope, const char *fmt, ...)
{
    mouse_trace_init();
    if (!g_mouse_trace_enabled || g_mouse_trace_file == NULL || fmt == NULL)
    {
        return;
    }

    char message[1024];
    va_list args;
    va_start(args, fmt);
    SDL_vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    char line[1280];
    const Uint64 ticks = SDL_GetTicks();
    const int written = SDL_snprintf(line, sizeof(line), "[%llu ms] %s: %s\n", (unsigned long long)ticks,
                                     scope != NULL ? scope : "mouse", message);
    if (written <= 0)
    {
        return;
    }

    const size_t bytes = (size_t)SDL_min(written, (int)sizeof(line) - 1);
    (void)SDL_WriteIO(g_mouse_trace_file, line, bytes);
    (void)SDL_FlushIO(g_mouse_trace_file);
}
