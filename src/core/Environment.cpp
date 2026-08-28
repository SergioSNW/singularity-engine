#include "Environment.h"

#include "Json.h"

#include <filesystem>
#include <fstream>
#include <sstream>

static json::Value Float3ToJson(const float v[3])
{
    json::Value arr = json::Value::MakeArray();
    for (int i = 0; i < 3; ++i)
        arr.array.push_back(json::Value::MakeNumber(v[i]));
    return arr;
}

static void ReadFloat3(const json::Value &root, const char *key, float out[3])
{
    const json::Value *v = root.Find(key);
    if (!v || !v->IsArray() || v->Size() < 3)
        return;
    for (int i = 0; i < 3; ++i)
    {
        const json::Value &e = v->At((size_t)i);
        if (e.IsNumber())
            out[i] = (float)e.num;
    }
}

static void EmitField(json::Value &root, const char *key, bool value)
{
    root.object.emplace_back(key, json::Value::MakeBool(value));
}

static void EmitField(json::Value &root, const char *key, float value)
{
    root.object.emplace_back(key, json::Value::MakeNumber(value));
}

std::string EnvironmentSettingsToJson(const EnvironmentSettings &env)
{
    json::Value root = json::Value::MakeObject();
    root.object.emplace_back("type", json::Value::MakeString("environment"));

    EmitField(root, "sky_enabled", env.sky_enabled);
    root.object.emplace_back("sky_color_top", Float3ToJson(env.sky_color_top));
    root.object.emplace_back("sky_color_horizon", Float3ToJson(env.sky_color_horizon));
    root.object.emplace_back("sky_sun_color", Float3ToJson(env.sky_sun_color));
    EmitField(root, "sky_sun_intensity", env.sky_sun_intensity);
    EmitField(root, "sky_sun_glow", env.sky_sun_glow);
    EmitField(root, "sky_sun_disk", env.sky_sun_disk);
    EmitField(root, "sky_sun_yaw", env.sky_sun_yaw);
    EmitField(root, "sky_sun_pitch", env.sky_sun_pitch);
    EmitField(root, "sky_star_intensity", env.sky_star_intensity);

    EmitField(root, "fog_enabled", env.fog_enabled);
    root.object.emplace_back("fog_color", Float3ToJson(env.fog_color));
    EmitField(root, "fog_density", env.fog_density);
    EmitField(root, "fog_height_falloff", env.fog_height_falloff);
    EmitField(root, "fog_start", env.fog_start);

    EmitField(root, "post_enabled", env.post_enabled);
    EmitField(root, "post_exposure", env.post_exposure);
    EmitField(root, "post_gamma", env.post_gamma);
    EmitField(root, "post_bloom_enabled", env.post_bloom_enabled);
    EmitField(root, "post_bloom_threshold", env.post_bloom_threshold);
    EmitField(root, "post_bloom_strength", env.post_bloom_strength);
    EmitField(root, "post_bloom_radius", env.post_bloom_radius);
    EmitField(root, "post_tonemap_enabled", env.post_tonemap_enabled);
    EmitField(root, "post_saturation", env.post_saturation);
    EmitField(root, "post_contrast", env.post_contrast);
    EmitField(root, "post_temperature", env.post_temperature);
    EmitField(root, "post_scale", env.post_scale);

    EmitField(root, "editor_fill_light_enabled", env.editor_fill_light_enabled);
    EmitField(root, "editor_fill_light_intensity", env.editor_fill_light_intensity);

    return json::WritePretty(root);
}

bool EnvironmentSettingsFromJson(const std::string &text, EnvironmentSettings &out,
                                 std::string *error)
{
    std::string parse_error;
    json::Value root = json::Parse(text, &parse_error);
    if (!root.IsObject())
    {
        if (error) *error = "environment file is not a JSON object" +
                            (parse_error.empty() ? std::string() : " (" + parse_error + ")");
        return false;
    }

    out.sky_enabled = root.Bool("sky_enabled", out.sky_enabled);
    ReadFloat3(root, "sky_color_top", out.sky_color_top);
    ReadFloat3(root, "sky_color_horizon", out.sky_color_horizon);
    ReadFloat3(root, "sky_sun_color", out.sky_sun_color);
    out.sky_sun_intensity = (float)root.Number("sky_sun_intensity", out.sky_sun_intensity);
    out.sky_sun_glow = (float)root.Number("sky_sun_glow", out.sky_sun_glow);
    out.sky_sun_disk = (float)root.Number("sky_sun_disk", out.sky_sun_disk);
    out.sky_sun_yaw = (float)root.Number("sky_sun_yaw", out.sky_sun_yaw);
    out.sky_sun_pitch = (float)root.Number("sky_sun_pitch", out.sky_sun_pitch);
    out.sky_star_intensity = (float)root.Number("sky_star_intensity", out.sky_star_intensity);

    out.fog_enabled = root.Bool("fog_enabled", out.fog_enabled);
    ReadFloat3(root, "fog_color", out.fog_color);
    out.fog_density = (float)root.Number("fog_density", out.fog_density);
    out.fog_height_falloff = (float)root.Number("fog_height_falloff", out.fog_height_falloff);
    out.fog_start = (float)root.Number("fog_start", out.fog_start);

    out.post_enabled = root.Bool("post_enabled", out.post_enabled);
    out.post_exposure = (float)root.Number("post_exposure", out.post_exposure);
    out.post_gamma = (float)root.Number("post_gamma", out.post_gamma);
    out.post_bloom_enabled = root.Bool("post_bloom_enabled", out.post_bloom_enabled);
    out.post_bloom_threshold = (float)root.Number("post_bloom_threshold", out.post_bloom_threshold);
    out.post_bloom_strength = (float)root.Number("post_bloom_strength", out.post_bloom_strength);
    out.post_bloom_radius = (float)root.Number("post_bloom_radius", out.post_bloom_radius);
    out.post_tonemap_enabled = root.Bool("post_tonemap_enabled", out.post_tonemap_enabled);
    out.post_saturation = (float)root.Number("post_saturation", out.post_saturation);
    out.post_contrast = (float)root.Number("post_contrast", out.post_contrast);
    out.post_temperature = (float)root.Number("post_temperature", out.post_temperature);
    out.post_scale = (float)root.Number("post_scale", out.post_scale);

    out.editor_fill_light_enabled = root.Bool("editor_fill_light_enabled", out.editor_fill_light_enabled);
    out.editor_fill_light_intensity = (float)root.Number("editor_fill_light_intensity", out.editor_fill_light_intensity);

    return true;
}

bool LoadEnvironmentAsset(const std::string &path, EnvironmentSettings &out,
                          std::string *error)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
    {
        if (error) *error = "cannot open environment asset '" + path + "'";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return EnvironmentSettingsFromJson(ss.str(), out, error);
}

bool SaveEnvironmentAsset(const std::string &path, const EnvironmentSettings &env,
                          std::string *error)
{
    const std::filesystem::path dir = std::filesystem::path(path).parent_path();
    std::error_code ec;
    if (!dir.empty())
        std::filesystem::create_directories(dir, ec);

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out)
    {
        if (error) *error = "cannot write environment asset '" + path + "'";
        return false;
    }
    out << EnvironmentSettingsToJson(env);
    out.close();
    if (!out.good())
    {
        if (error) *error = "failed writing environment asset '" + path + "'";
        return false;
    }
    if (error) error->clear();
    return true;
}
