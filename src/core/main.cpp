#include <SDL.h>

#include "Application.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    Application app;

    if (!app.Init(1280, 720, "Singularity Engine v0.1.0-alpha"))
        return 1;

    app.Run();

    return 0;
}
