#pragma once

#include <string>

#include "Json.h"

class Scene;

// Serializes a Scene graph to / from a JSON document. The on-disk format is a
// flat array of entity records whose parent-child links are expressed through
// stable UUID strings; see ENGINE_TEXTBOOK.md, Chapter 8, for the schema.
//
// All functions are pure and report failures through an optional error string
// rather than exceptions, so the editor can surface a clean message in the UI.
class SceneSerializer
{
public:
    static bool SaveToFile(const Scene &scene, const std::string &path, std::string *error = nullptr);
    static bool LoadFromFile(Scene &scene, const std::string &path, std::string *error = nullptr);

    static json::Value SerializeScene(const Scene &scene);
    static bool DeserializeScene(Scene &scene, const json::Value &root, std::string *error = nullptr);
};
