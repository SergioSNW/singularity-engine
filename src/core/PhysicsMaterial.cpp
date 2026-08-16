#include "PhysicsMaterial.h"

#include "Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

void CombinePhysicsMaterials(const PhysicsMaterial &a, const PhysicsMaterial &b,
                             float &out_friction, float &out_restitution)
{
    out_friction = std::sqrt(std::clamp(a.friction, 0.0f, 1.0f) *
                             std::clamp(b.friction, 0.0f, 1.0f));
    out_restitution = std::max(std::clamp(a.restitution, 0.0f, 1.0f),
                               std::clamp(b.restitution, 0.0f, 1.0f));
}

std::string PhysicsMaterialToJson(const PhysicsMaterial &material)
{
    json::Value root = json::Value::MakeObject();
    root.object.emplace_back("name", json::Value::MakeString(material.name));
    root.object.emplace_back("friction", json::Value::MakeNumber(material.friction));
    root.object.emplace_back("restitution", json::Value::MakeNumber(material.restitution));
    return json::WritePretty(root);
}

bool PhysicsMaterialFromJson(const std::string &text, PhysicsMaterial &out,
                             std::string *error)
{
    std::string parse_error;
    json::Value root = json::Parse(text, &parse_error);
    if (!root.IsObject())
    {
        if (error) *error = "physics material file is not a JSON object" +
                            (parse_error.empty() ? std::string() : " (" + parse_error + ")");
        return false;
    }

    out.name = root.String("name", "");
    out.friction = (float)root.Number("friction", out.friction);
    out.restitution = (float)root.Number("restitution", out.restitution);
    return true;
}

// --- PhysicsMaterialLibrary ---

static const PhysicsMaterial &DefaultPhysicsMaterial()
{
    static PhysicsMaterial mat;  // friction 0.5, restitution 0.1
    return mat;
}

PhysicsMaterialLibrary::PhysicsMaterialLibrary()
{
    m_materials.emplace("__default__", DefaultPhysicsMaterial());
}

const PhysicsMaterial* PhysicsMaterialLibrary::Load(const std::string &path,
                                                    std::string *error)
{
    auto it = m_materials.find(path);
    if (it != m_materials.end())
        return &it->second;

    std::string resolved = path;
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec))
    {
        const std::string alt = "assets/physics/" + path;
        if (std::filesystem::exists(alt, ec))
            resolved = alt;
        else
        {
            if (error) *error = "physics material file not found: '" + path + "'";
            return nullptr;
        }
    }

    std::ifstream in(resolved, std::ios::binary);
    if (!in)
    {
        if (error) *error = "cannot open physics material file '" + resolved + "'";
        return nullptr;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    PhysicsMaterial mat;
    if (!PhysicsMaterialFromJson(ss.str(), mat, error))
        return nullptr;
    if (mat.name.empty())
        mat.name = std::filesystem::path(resolved).stem().string();
    if (error) error->clear();

    auto res = m_materials.emplace(path, std::move(mat));
    return &res.first->second;
}

const PhysicsMaterial* PhysicsMaterialLibrary::Get(const std::string &key) const
{
    auto it = m_materials.find(key);
    return (it == m_materials.end()) ? nullptr : &it->second;
}

const PhysicsMaterial* PhysicsMaterialLibrary::GetDefault() const
{
    return Get("__default__");
}

const PhysicsMaterial* PhysicsMaterialLibrary::Create(const std::string &filename,
                                                      const PhysicsMaterial &material,
                                                      std::string *error)
{
    const std::string dir = "assets/physics";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::string path = dir + "/" + filename;
    PhysicsMaterial copy = material;
    if (copy.name.empty())
        copy.name = std::filesystem::path(filename).stem().string();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (error) *error = "cannot write physics material file '" + path + "'";
        return nullptr;
    }
    out << PhysicsMaterialToJson(copy);
    out.close();
    if (!out.good())
    {
        if (error) *error = "failed writing physics material file '" + path + "'";
        return nullptr;
    }

    auto res = m_materials.emplace(path, std::move(copy));
    if (error) error->clear();
    return &res.first->second;
}

const PhysicsMaterial* PhysicsMaterialLibrary::Save(const std::string &filename,
                                                    const PhysicsMaterial &material,
                                                    std::string *error)
{
    const std::string dir = "assets/physics";
    const std::string path = dir + "/" + filename;

    PhysicsMaterial copy = material;
    if (copy.name.empty())
        copy.name = std::filesystem::path(filename).stem().string();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (error) *error = "cannot write physics material file '" + path + "'";
        return nullptr;
    }
    out << PhysicsMaterialToJson(copy);
    out.close();
    if (!out.good())
    {
        if (error) *error = "failed writing physics material file '" + path + "'";
        return nullptr;
    }

    // Refresh every cached copy of this asset: Load() caches under the bare
    // filename while Create() uses the prefixed path, so both keys may exist.
    auto it = m_materials.find(path);
    if (it != m_materials.end())
        it->second = copy;
    it = m_materials.find(filename);
    if (it != m_materials.end())
        it->second = copy;

    if (error) error->clear();
    const PhysicsMaterial *result = Get(path);
    return result ? result : Get(filename);
}
