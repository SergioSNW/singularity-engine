#include "AssetImporter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{

std::string Lower(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

bool EndsWith(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsImageExt(const std::string &ext)
{
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".bmp" || ext == ".tga" || ext == ".gif";
}

// Normalize a target folder to a relative path under "assets/" with forward
// slashes and no leading/trailing separator: "meshes", "assets/meshes" and
// "assets/meshes/props" -> "meshes" / "meshes" / "meshes/props". The assets/
// root itself normalizes to "" and is rejected by Import().
std::string NormalizeDir(const std::string &dir)
{
    std::string d = fs::path(dir).generic_string();
    while (!d.empty() && d.front() == '/')
        d.erase(d.begin());
    while (!d.empty() && d.back() == '/')
        d.pop_back();
    if (d == "assets" || d.rfind("assets/", 0) == 0)
        d = (d == "assets") ? std::string() : d.substr(7);
    return d;
}

} // namespace

namespace AssetImporter
{

std::string ClassifyDir(const std::string &src_path)
{
    const std::string lower = Lower(fs::path(src_path).filename().string());
    const std::string ext = Lower(fs::path(src_path).extension().string());
    if (ext == ".obj")        return "meshes";
    if (ext == ".mat")        return "materials";
    if (IsImageExt(ext))      return "textures";
    if (ext == ".lua")        return "scripts";
    if (ext == ".json")
        return EndsWith(lower, ".prefab.json") ? "prefabs" : "scenes";
    return "";
}

Result Import(const std::string &src_path, const std::string &dir)
{
    Result r;

    std::error_code ec;
    if (!fs::exists(src_path, ec) || !fs::is_regular_file(src_path, ec))
    {
        r.error = "source file not found: '" + src_path + "'";
        return r;
    }

    const std::string rel = NormalizeDir(dir);
    if (rel.empty())
    {
        r.error = "no assets sub-folder for '" + src_path + "'";
        return r;
    }

    const std::string folder = "assets/" + rel;
    fs::create_directories(folder, ec);

    const std::string filename = fs::path(src_path).filename().string();
    std::string dest = folder + "/" + filename;
    const std::string stem = fs::path(filename).stem().string();
    const std::string ext = fs::path(filename).extension().string();
    int counter = 1;
    while (fs::exists(dest, ec))
    {
        dest = folder + "/" + stem + "_" + std::to_string(counter) + ext;
        ++counter;
    }

    std::error_code copy_ec;
    fs::copy_file(src_path, dest, fs::copy_options::none, copy_ec);
    if (copy_ec)
    {
        r.error = "copy failed: " + copy_ec.message();
        return r;
    }

    r.ok = true;
    r.dest = fs::path(dest).generic_string();
    r.name = filename;
    return r;
}

Result Import(const std::string &src_path)
{
    return Import(src_path, ClassifyDir(src_path));
}

} // namespace AssetImporter
