#include "Material.h"

#include "Json.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

static std::string TrimString(std::string s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos)
        return std::string();
    return s.substr(b, e - b + 1);
}

std::string MaterialToJson(const Material &material)
{
    json::Value root = json::Value::MakeObject();
    root.object.emplace_back("name", json::Value::MakeString(material.name));
    json::Value color = json::Value::MakeArray();
    for (int i = 0; i < 4; ++i)
        color.array.push_back(json::Value::MakeNumber(material.color[i]));
    root.object.emplace_back("color", std::move(color));
    root.object.emplace_back("texture", json::Value::MakeString(material.texture));
    root.object.emplace_back("albedo_multiplier",
                            json::Value::MakeNumber(material.albedo_multiplier));
    root.object.emplace_back("normal_texture",
                            json::Value::MakeString(material.normal_texture));
    root.object.emplace_back("normal_strength",
                            json::Value::MakeNumber(material.normal_strength));
    root.object.emplace_back("metallic", json::Value::MakeNumber(material.metallic));
    root.object.emplace_back("metallic_texture",
                            json::Value::MakeString(material.metallic_texture));
    root.object.emplace_back("metallic_multiplier",
                            json::Value::MakeNumber(material.metallic_multiplier));
    root.object.emplace_back("roughness", json::Value::MakeNumber(material.roughness));
    root.object.emplace_back("roughness_texture",
                            json::Value::MakeString(material.roughness_texture));
    root.object.emplace_back("roughness_multiplier",
                            json::Value::MakeNumber(material.roughness_multiplier));
    root.object.emplace_back("ao", json::Value::MakeNumber(material.ao));
    root.object.emplace_back("ao_texture", json::Value::MakeString(material.ao_texture));
    root.object.emplace_back("ao_multiplier",
                            json::Value::MakeNumber(material.ao_multiplier));
    root.object.emplace_back("shininess", json::Value::MakeNumber(material.shininess));
    return json::WritePretty(root);
}

bool MaterialFromJson(const std::string &text, Material &out, std::string *error)
{
    std::string parse_error;
    json::Value root = json::Parse(text, &parse_error);
    if (!root.IsObject())
    {
        if (error) *error = "material file is not a JSON object" +
                            (parse_error.empty() ? std::string() : " (" + parse_error + ")");
        return false;
    }

    out.name = root.String("name", "");
    if (const json::Value *c = root.Find("color"))
    {
        if (c->IsArray())
        {
            for (int i = 0; i < 4; ++i)
            {
                const json::Value &e = c->At((size_t)i);
                out.color[i] = e.IsNumber() ? (float)e.num : 1.0f;
            }
        }
    }
    out.texture = root.String("texture", "");
    out.albedo_multiplier = (float)root.Number("albedo_multiplier", 1.0);
    out.normal_texture = root.String("normal_texture", "");
    out.normal_strength = (float)root.Number("normal_strength", 1.0);
    out.metallic = (float)root.Number("metallic", 0.0);
    out.metallic_texture = root.String("metallic_texture", "");
    out.metallic_multiplier = (float)root.Number("metallic_multiplier", 1.0);
    out.roughness = (float)root.Number("roughness", 0.5);
    out.roughness_texture = root.String("roughness_texture", "");
    out.roughness_multiplier = (float)root.Number("roughness_multiplier", 1.0);
    out.ao = (float)root.Number("ao", 1.0);
    out.ao_texture = root.String("ao_texture", "");
    out.ao_multiplier = (float)root.Number("ao_multiplier", 1.0);
    out.shininess = (float)root.Number("shininess", 0.0);
    return true;
}

// --- MaterialLibrary ---

static const Material &DefaultMaterial()
{
    static Material mat;
    return mat;
}

MaterialLibrary::MaterialLibrary()
{
    m_materials.emplace("__default__", DefaultMaterial());
}

const Material* MaterialLibrary::Load(const std::string &path, std::string *error)
{
    auto it = m_materials.find(path);
    if (it != m_materials.end())
        return &it->second;

    std::string resolved = path;
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec))
    {
        const std::string alt = "assets/materials/" + path;
        if (std::filesystem::exists(alt, ec))
            resolved = alt;
        else
        {
            if (error) *error = "material file not found: '" + path + "'";
            return nullptr;
        }
    }

    std::ifstream in(resolved, std::ios::binary);
    if (!in)
    {
        if (error) *error = "cannot open material file '" + resolved + "'";
        return nullptr;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    Material mat;
    if (!MaterialFromJson(ss.str(), mat, error))
        return nullptr;
    if (mat.name.empty())
        mat.name = std::filesystem::path(resolved).stem().string();
    if (error) error->clear();

    auto res = m_materials.emplace(path, std::move(mat));
    return &res.first->second;
}

const Material* MaterialLibrary::Get(const std::string &key) const
{
    auto it = m_materials.find(key);
    return (it == m_materials.end()) ? nullptr : &it->second;
}

const Material* MaterialLibrary::GetDefault() const
{
    return Get("__default__");
}

const Material* MaterialLibrary::Create(const std::string &filename,
                                        const Material &material, std::string *error)
{
    const std::string dir = "assets/materials";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::string path = dir + "/" + filename;
    Material copy = material;
    if (copy.name.empty())
        copy.name = std::filesystem::path(filename).stem().string();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (error) *error = "cannot write material file '" + path + "'";
        return nullptr;
    }
    out << MaterialToJson(copy);
    out.close();
    if (!out.good())
    {
        if (error) *error = "failed writing material file '" + path + "'";
        return nullptr;
    }

    auto res = m_materials.emplace(path, std::move(copy));
    if (error) error->clear();
    return &res.first->second;
}

const Material* MaterialLibrary::Save(const std::string &filename,
                                      const Material &material, std::string *error)
{
    const std::string dir = "assets/materials";
    const std::string path = dir + "/" + filename;

    Material copy = material;
    if (copy.name.empty())
        copy.name = std::filesystem::path(filename).stem().string();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (error) *error = "cannot write material file '" + path + "'";
        return nullptr;
    }
    out << MaterialToJson(copy);
    out.close();
    if (!out.good())
    {
        if (error) *error = "failed writing material file '" + path + "'";
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
    const Material *result = Get(path);
    return result ? result : Get(filename);
}

const Material* MaterialLibrary::LiveUpdate(const std::string &filename,
                                            const Material &material)
{
    const std::string path = "assets/materials/" + filename;

    // Mirror Save()'s key bookkeeping but stay in memory: the working copy in
    // the editor may carry a temporary name, so a blank name inherits the
    // asset stem like the on-disk paths do.
    Material copy = material;
    if (copy.name.empty())
        copy.name = std::filesystem::path(filename).stem().string();

    const Material *result = nullptr;
    auto it = m_materials.find(path);
    if (it != m_materials.end())
    {
        it->second = copy;
        result = &it->second;
    }
    it = m_materials.find(filename);
    if (it != m_materials.end())
    {
        it->second = copy;
        result = &it->second;
    }
    return result;
}
