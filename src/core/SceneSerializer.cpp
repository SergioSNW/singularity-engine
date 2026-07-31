#include "SceneSerializer.h"

#include "Scene.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

// --- small helpers for building / reading typed JSON nodes ---

json::Value Vec3ToJson(const float v[3])
{
    json::Value arr = json::Value::MakeArray();
    arr.array.push_back(json::Value::MakeNumber(v[0]));
    arr.array.push_back(json::Value::MakeNumber(v[1]));
    arr.array.push_back(json::Value::MakeNumber(v[2]));
    return arr;
}

json::Value Vec4ToJson(const float v[4])
{
    json::Value arr = json::Value::MakeArray();
    for (int i = 0; i < 4; ++i)
        arr.array.push_back(json::Value::MakeNumber(v[i]));
    return arr;
}

void Vec3FromJson(float dst[3], const json::Value *node)
{
    if (!node || !node->IsArray())
        return;
    for (int i = 0; i < 3 && i < (int)node->array.size(); ++i)
        if (node->array[i].IsNumber())
            dst[i] = (float)node->array[i].num;
}

void Vec4FromJson(float dst[4], const json::Value *node)
{
    if (!node || !node->IsArray())
        return;
    for (int i = 0; i < 4 && i < (int)node->array.size(); ++i)
        if (node->array[i].IsNumber())
            dst[i] = (float)node->array[i].num;
}

} // namespace

json::Value SceneSerializer::SerializeScene(const Scene &scene)
{
    json::Value root = json::Value::MakeObject();
    root.object.emplace_back("engine", json::Value::MakeString("singularity-engine"));
    root.object.emplace_back("version", json::Value::MakeNumber(1.0));

    json::Value entities = json::Value::MakeArray();
    for (const auto &entity_ptr : scene.GetEntities())
    {
        const Entity &e = *entity_ptr;

        json::Value ent = json::Value::MakeObject();
        ent.object.emplace_back("name", json::Value::MakeString(e.tag.tag));
        ent.object.emplace_back("uuid", json::Value::MakeString(e.uuid));
        ent.object.emplace_back(
            "parent",
            e.parent ? json::Value::MakeString(e.parent->uuid) : json::Value::MakeNull()
        );

        json::Value transform = json::Value::MakeObject();
        transform.object.emplace_back("position", Vec3ToJson(e.transform.position));
        transform.object.emplace_back("rotation", Vec3ToJson(e.transform.rotation));
        transform.object.emplace_back("scale", Vec3ToJson(e.transform.scale));
        ent.object.emplace_back("transform", std::move(transform));

        json::Value material = json::Value::MakeObject();
        material.object.emplace_back("color", Vec4ToJson(e.material.color));
        material.object.emplace_back("active", json::Value::MakeBool(e.material.active));
        ent.object.emplace_back("material", std::move(material));

        json::Value camera = json::Value::MakeObject();
        camera.object.emplace_back("fov", json::Value::MakeNumber(e.camera.fov));
        camera.object.emplace_back("near_plane", json::Value::MakeNumber(e.camera.near_plane));
        camera.object.emplace_back("far_plane", json::Value::MakeNumber(e.camera.far_plane));
        camera.object.emplace_back("pitch", json::Value::MakeNumber(e.camera.pitch));
        camera.object.emplace_back("yaw", json::Value::MakeNumber(e.camera.yaw));
        camera.object.emplace_back("primary", json::Value::MakeBool(e.camera.primary));
        ent.object.emplace_back("camera", std::move(camera));

        entities.array.push_back(std::move(ent));
    }

    root.object.emplace_back("entities", std::move(entities));
    return root;
}

bool SceneSerializer::DeserializeScene(Scene &scene, const json::Value &root, std::string *error)
{
    if (!root.IsObject())
    {
        if (error) *error = "scene file: root must be an object";
        return false;
    }

    const json::Value *entities = root.Find("entities");
    if (!entities || !entities->IsArray())
    {
        if (error) *error = "scene file: missing 'entities' array";
        return false;
    }

    scene.Clear();

    // Pass 1: create every entity and load its components. UUIDs are the
    // persistent identity; runtime int ids are reassigned by CreateEntity.
    for (const json::Value &ent : entities->array)
    {
        if (!ent.IsObject())
            continue;

        Entity &e = scene.CreateEntity(ent.String("name", "Entity"));
        e.uuid = ent.String("uuid", e.uuid);

        if (const json::Value *tf = ent.Find("transform"); tf && tf->IsObject())
        {
            Vec3FromJson(e.transform.position, tf->Find("position"));
            Vec3FromJson(e.transform.rotation, tf->Find("rotation"));
            Vec3FromJson(e.transform.scale, tf->Find("scale"));
        }

        if (const json::Value *mat = ent.Find("material"); mat && mat->IsObject())
        {
            Vec4FromJson(e.material.color, mat->Find("color"));
            e.material.active = mat->Bool("active", e.material.active);
        }

        if (const json::Value *cam = ent.Find("camera"); cam && cam->IsObject())
        {
            e.camera.fov        = (float)cam->Number("fov", e.camera.fov);
            e.camera.near_plane = (float)cam->Number("near_plane", e.camera.near_plane);
            e.camera.far_plane  = (float)cam->Number("far_plane", e.camera.far_plane);
            e.camera.pitch      = (float)cam->Number("pitch", e.camera.pitch);
            e.camera.yaw        = (float)cam->Number("yaw", e.camera.yaw);
            e.camera.primary    = cam->Bool("primary", e.camera.primary);
        }
    }

    // Pass 2: resolve parent-child links by UUID. Two passes make the file
    // order-independent — a child may reference a parent declared later.
    for (const json::Value &ent : entities->array)
    {
        const std::string uuid   = ent.String("uuid", "");
        const std::string parent = ent.String("parent", "");

        if (uuid.empty() || parent.empty())
            continue;

        Entity *child = nullptr;
        Entity *ancestor = nullptr;
        for (auto &candidate : scene.GetEntities())
        {
            if (candidate->uuid == uuid)
                child = candidate.get();
            else if (candidate->uuid == parent)
                ancestor = candidate.get();
            if (child && ancestor)
                break;
        }

        if (child && ancestor && child != ancestor)
            scene.SetParent(child->id, ancestor->id);
    }

    return true;
}

bool SceneSerializer::SaveToFile(const Scene &scene, const std::string &path, std::string *error)
{
    std::error_code ec;
    const std::filesystem::path file_path(path);
    const std::filesystem::path dir = file_path.parent_path();
    if (!dir.empty())
        std::filesystem::create_directories(dir, ec);

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out)
    {
        if (error) *error = "cannot open '" + path + "' for writing";
        return false;
    }

    out << json::WritePretty(SerializeScene(scene)) << "\n";
    out.close();

    if (error) error->clear();
    return true;
}

bool SceneSerializer::LoadFromFile(Scene &scene, const std::string &path, std::string *error)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
    {
        if (error) *error = "cannot open '" + path + "' for reading";
        return false;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    std::string parse_error;
    json::Value root = json::Parse(text, &parse_error);
    if (!root.IsObject())
    {
        if (error) *error = "scene file '" + path + "': " + parse_error;
        return false;
    }

    return DeserializeScene(scene, root, error);
}
