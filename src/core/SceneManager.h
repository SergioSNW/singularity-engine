#pragma once

#include <memory>
#include <string>

class Scene;

// Owns the engine's single active Scene and all file-backed transitions
// between maps. The Scene object itself is allocated once and never replaced:
// LoadScene/NewScene clear and rebuild it *in place*, so every subsystem that
// holds a Scene* (panels, gizmo, physics, script session) keeps a valid
// pointer across a scene switch.
class SceneManager
{
public:
    SceneManager();

    // The active scene (never null).
    Scene *GetScene() { return m_scene.get(); }
    const Scene *GetScene() const { return m_scene.get(); }

    // Replace the active scene contents with the map at `filepath`. On success
    // the active path/name track the loaded file. The scene's "meta" name
    // falls back to the file stem when the file carries none.
    bool LoadScene(const std::string &filepath, std::string *error = nullptr);

    // Serialize the active scene to `filepath`, stamping map metadata (name
    // from the file stem, creation date on first save) when absent.
    bool SaveScene(const std::string &filepath, std::string *error = nullptr);

    // Start from a blank map (a default camera is created so the editor has a
    // viewpoint). Active path is cleared; the meta name becomes "Untitled".
    bool NewScene(std::string *error = nullptr);

    const std::string &ActivePath() const { return m_path; }
    const std::string &ActiveName() const { return m_name; }

private:
    std::unique_ptr<Scene> m_scene;
    std::string m_path;   // filepath of the active map ("" for new/unsaved)
    std::string m_name;   // display name (file stem or "Untitled Scene")
};
