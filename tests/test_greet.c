#include "sdl3d/sdl3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *greeting = sdl3d_greet();

    if (strcmp(greeting, "Hello from SDL3D.") != 0)
    {
        fprintf(stderr, "unexpected greeting: %s\n", greeting);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
