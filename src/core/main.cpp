#include <SDL.h>

#include "Application.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    Application app;

    // 1580x1020 primary window (1280x720 + 300px each) for a roomier
    // dockspace canvas.
    if (!app.Init(1580, 1020, "Singularity Engine v0.48.0-alpha"))
        return 1;

    app.Run();

    return 0;
}
