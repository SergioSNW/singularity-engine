#pragma once

#include <string>

// OS file-drop ingestion (Phase 23): classify external files dragged from the
// host file explorer by extension and copy them into the matching assets/<subdir>/
// so the Content Browser and the asset libraries resolve them as ordinary
// project assets:
//
//   .obj                        -> assets/meshes/
//   .mat                        -> assets/materials/
//   .png/.jpg/.jpeg/.bmp/.tga/.gif -> assets/textures/
//   .lua                        -> assets/scripts/
//   *.prefab.json               -> assets/prefabs/
//   .json (otherwise)           -> assets/scenes/
//
// Everything else is unsupported and is reported through the result's `error`.
// Colliding file names are renamed ("name_1.ext", "name_2.ext", ...) so an
// existing asset is never overwritten. The module is pure filesystem code
// (no SDL/ImGui), so it is independently testable headless.
namespace AssetImporter
{
    struct Result
    {
        bool ok = false;
        std::string dest;   // relative destination, e.g. "assets/meshes/foo.obj"
        std::string name;   // leaf file name, e.g. "foo.obj"
        std::string error;
    };

    // Sub-folder (relative to assets/) the file belongs in; "" when unsupported.
    std::string ClassifyDir(const std::string &src_path);

    // Copy `src_path` into assets/<dir>/ with a collision-free name. `dir` may
    // be a bare sub-folder ("meshes"), a full path under assets
    // ("assets/meshes") or a nested path ("assets/meshes/props"); the assets/
    // root itself is rejected so imports always land in a typed folder.
    Result Import(const std::string &src_path, const std::string &dir);

    // Import into the folder ClassifyDir() chose for the file type.
    Result Import(const std::string &src_path);
}
