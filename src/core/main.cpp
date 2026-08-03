#include <SDL.h>

#include "Application.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    Application app;

    if (!app.Init(1280, 720, "Singularity Engine v0.10.3-alpha"))
        return 1;

    app.Run();

    return 0;
}
