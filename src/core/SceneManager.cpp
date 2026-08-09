#include "SceneManager.h"

#include "Scene.h"
#include "SceneSerializer.h"

#include <chrono>
#include <ctime>
#include <filesystem>

namespace {

// Current local date as "YYYY-MM-DD", stamped into scene meta on first save.
std::string TodayIsoDate()
{
    const std::time_t now = std::time(nullptr);
    std::tm t = {};
#if defined(_WIN32)
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return std::string(buf);
}

} // namespace

SceneManager::SceneManager()
    : m_scene(std::make_unique<Scene>())
    , m_name("Untitled Scene")
{
}

bool SceneManager::LoadScene(const std::string &filepath, std::string *error)
{
    // Load INTO the same Scene object (DeserializeScene clears it first), so
    // external Scene* pointers stay valid across the switch.
    if (!SceneSerializer::LoadFromFile(*m_scene, filepath, error))
        return false;

    // Level-design guarantee: every scene (new or loaded) carries at least one
    // active light, so freshly opened maps are never stuck in the dark.
    EnsureActiveLight(*m_scene);

    m_path = filepath;
    m_name = std::filesystem::path(filepath).stem().string();
    if (m_scene->Meta().name.empty())
        m_scene->Meta().name = m_name;

    if (error) error->clear();
    return true;
}

bool SceneManager::SaveScene(const std::string &filepath, std::string *error)
{
    // Stamp map metadata: name falls back to the file stem, and the creation
    // date is written on the first save of a scene.
    if (m_scene->Meta().name.empty())
        m_scene->Meta().name = std::filesystem::path(filepath).stem().string();
    if (m_scene->Meta().created.empty())
        m_scene->Meta().created = TodayIsoDate();

    if (!SceneSerializer::SaveToFile(*m_scene, filepath, error))
        return false;

    m_path = filepath;
    m_name = m_scene->Meta().name;
    return true;
}

bool SceneManager::NewScene(std::string *error)
{
    m_scene->Clear();
    Entity &camera = m_scene->CreateEntity("Camera");
    camera.transform.position[1] = 2.0f;
    camera.transform.position[2] = 8.0f;
    camera.camera.pitch = -14.0f;
    EnsureActiveLight(*m_scene);
    m_scene->Meta().name = "Untitled Scene";
    m_scene->Meta().author.clear();
    m_scene->Meta().created.clear();

    m_path.clear();
    m_name = "Untitled Scene";

    if (error) error->clear();
    return true;
}
