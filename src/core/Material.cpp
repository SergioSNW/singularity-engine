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
