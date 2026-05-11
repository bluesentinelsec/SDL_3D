#ifndef SLAYER3D_MOUSE_TRACE_INTERNAL_H
#define SLAYER3D_MOUSE_TRACE_INTERNAL_H

#include <stdbool.h>

bool slayer3d_mouse_trace_enabled(void);
void slayer3d_mouse_tracef(const char *scope, const char *fmt, ...);

#endif
