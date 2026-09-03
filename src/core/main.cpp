#include <SDL.h>

#include "Application.h"

#include <cstdio>
#include <filesystem>
#include <string>

int main(int argc, char *argv[])
{
    // Resolve the working directory to the executable's own folder before
    // anything else touches a relative "assets/..." path. Without this, an
    // exported build only works when launched in a way that happens to set
    // the CWD to its own folder (a plain double-click does; a shortcut with
    // a different "Start in" target, or running it from a shell in some
    // other directory, would not) -- SDL_GetBasePath() is the portable way
    // to find where the binary actually lives, independent of how it was
    // invoked.
    if (char *base_path = SDL_GetBasePath())
    {
        std::error_code ec;
        std::filesystem::current_path(base_path, ec);
        SDL_free(base_path);
    }

    // Stage 5 export pipeline: `--play <scene>` boots straight into that
    // scene's Play session with zero editor UI, ever. Nothing else on the
    // command line is recognized (there is no other CLI surface today).
    std::string play_scene;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--play")
            play_scene = argv[i + 1];

    // Auto-detect an exported build: with no explicit --play, a `game.scene`
    // sitting next to the executable (written by Application::ExportBuild)
    // means this copy of the binary *is* a shipped game, not the editor --
    // so a double-click with no arguments at all still boots straight into
    // Play mode. A development build never has this file next to it.
    if (play_scene.empty() && std::filesystem::exists("game.scene"))
        play_scene = "game.scene";

    Application app;

    if (!play_scene.empty())
    {
        std::string error;
        if (!app.InitRuntime(1280, 720, "Singularity Engine", play_scene, &error))
        {
            std::fprintf(stderr, "Failed to start '%s': %s\n", play_scene.c_str(), error.c_str());
            return 1;
        }
    }
    else
    {
        // 1580x1020 primary window (1280x720 + 300px each) for a roomier
        // dockspace canvas.
        if (!app.Init(1580, 1020, "Singularity Engine v0.51.0-alpha"))
            return 1;
    }

    app.Run();

    return 0;
}
