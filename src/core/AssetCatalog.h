#pragma once

#include <string>
#include <vector>

// Pure asset taxonomy shared by the Content Browser (thumbnail kind, category
// chips, search matching, breadcrumbs) and the standalone phase31 harness.
// No SDL, no ImGui, no filesystem I/O -- just string logic, so it links and
// tests headless. Mirrors the AssetImporter extension mapping so classification
// stays consistent between OS file drops and the browser UI.
namespace AssetCatalog
{

enum class AssetKind
{
    Folder,   // directory entry (not a file classification)
    Scene,    // .json scene document
    Prefab,   // .json prefab document (<name>.prefab.json convention)
    Script,   // .lua
    Mesh,     // .obj
    Material, // .mat
    Texture,  // image (bmp/png/jpg/jpeg/tga/gif)
    Audio,    // .wav / .ogg
    Other,
};

// Classify a path by its extension (case-insensitive). Prefabs use the
// "<name>.prefab.json" naming convention, exactly like AssetImporter; any
// other .json is a scene. Unknown extensions fall through to Other.
inline AssetKind ClassifyAsset(const std::string &path)
{
    std::string lower;
    lower.reserve(path.size());
    for (char c : path)
        lower += (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;

    auto has_suffix = [&](const std::string &s) {
        return lower.size() >= s.size() &&
               lower.compare(lower.size() - s.size(), s.size(), s) == 0;
    };

    if (has_suffix(".prefab.json")) return AssetKind::Prefab;
    if (has_suffix(".json"))        return AssetKind::Scene;
    if (has_suffix(".lua"))         return AssetKind::Script;
    if (has_suffix(".obj"))         return AssetKind::Mesh;
    if (has_suffix(".mat"))         return AssetKind::Material;
    if (has_suffix(".bmp") || has_suffix(".png") || has_suffix(".jpg") ||
        has_suffix(".jpeg") || has_suffix(".tga") || has_suffix(".gif"))
        return AssetKind::Texture;
    if (has_suffix(".wav") || has_suffix(".ogg"))
        return AssetKind::Audio;
    return AssetKind::Other;
}

inline const char *AssetKindLabel(AssetKind kind)
{
    switch (kind)
    {
        case AssetKind::Folder:   return "Folder";
        case AssetKind::Scene:    return "Scene";
        case AssetKind::Prefab:   return "Prefab";
        case AssetKind::Script:   return "Script";
        case AssetKind::Mesh:     return "Mesh";
        case AssetKind::Material: return "Material";
        case AssetKind::Texture:  return "Texture";
        case AssetKind::Audio:    return "Audio";
        default:                  return "Other";
    }
}

// Content Browser category chips.
enum class AssetFilter
{
    All,
    Meshes,
    Materials,
    Textures,
    Audio,
    Prefabs,
};

// True when `kind` should stay visible under `filter`. Folders always pass so
// navigation survives an active category chip; every other kind must match.
inline bool AssetPassesFilter(AssetKind kind, AssetFilter filter)
{
    if (filter == AssetFilter::All)
        return true;
    if (kind == AssetKind::Folder)
        return true;
    switch (filter)
    {
        case AssetFilter::Meshes:    return kind == AssetKind::Mesh;
        case AssetFilter::Materials: return kind == AssetKind::Material;
        case AssetFilter::Textures:  return kind == AssetKind::Texture;
        case AssetFilter::Audio:     return kind == AssetKind::Audio;
        case AssetFilter::Prefabs:   return kind == AssetKind::Prefab;
        default:                     return true;
    }
}

inline const char *AssetFilterLabel(AssetFilter filter)
{
    switch (filter)
    {
        case AssetFilter::All:       return "All";
        case AssetFilter::Meshes:    return "Meshes";
        case AssetFilter::Materials: return "Materials";
        case AssetFilter::Textures:  return "Textures";
        case AssetFilter::Audio:     return "Audio";
        case AssetFilter::Prefabs:   return "Prefabs";
        default:                     return "All";
    }
}

// Case-insensitive substring match of `query` against `name`. An empty query
// always matches (the search box is "off").
inline bool NameMatches(const std::string &name, const std::string &query)
{
    if (query.empty())
        return true;
    auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    };
    if (name.size() < query.size())
        return false;
    for (size_t i = 0; i + query.size() <= name.size(); ++i)
    {
        bool ok = true;
        for (size_t j = 0; j < query.size(); ++j)
        {
            if (lower(name[i + j]) != lower(query[j]))
            {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
    }
    return false;
}

// Split `path` (forward slashes) into clickable breadcrumb segments, each a
// cumulative prefix: "assets/meshes/props" -> ["assets", "assets/meshes",
// "assets/meshes/props"]. An empty path yields an empty result.
inline std::vector<std::string> BreadcrumbSegments(const std::string &path)
{
    std::vector<std::string> out;
    if (path.empty())
        return out;
    std::string prefix;
    size_t pos = 0;
    while (pos <= path.size())
    {
        const size_t slash = path.find('/', pos);
        const std::string part = path.substr(
            pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if (!part.empty())
            prefix = prefix.empty() ? part : prefix + "/" + part;
        out.push_back(prefix);
        if (slash == std::string::npos)
            break;
        pos = slash + 1;
    }
    return out;
}

} // namespace AssetCatalog
