#pragma once

#include <string>

#include "Json.h"

class Scene;
struct Entity;

// Serializes a Scene graph to / from a JSON document. The on-disk format is a
// flat array of entity records whose parent-child links are expressed through
// stable UUID strings; see ENGINE_TEXTBOOK.md, Chapter 8, for the schema.
// Prefabs reuse the same per-entity component encoding as a single-entity
// tree: a `{ "prefab": true, "name": ..., "root": { ...entity+children } }`
// document that can be instantiated any number of times (fresh UUIDs, ids,
// and runtime ids are assigned on each spawn).
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

    // --- Prefabs ---
    // Write `entity` (and its descendants) as a reusable prefab document.
    static bool SavePrefab(const Entity &entity, const std::string &path, std::string *error = nullptr);
    // Instantiate a prefab file into `scene`, optionally under `parent`.
    // Returns the spawned root entity (nullptr on failure).
    static Entity *LoadPrefab(Scene &scene, const std::string &path,
                              Entity *parent = nullptr, std::string *error = nullptr);
    // True when the file is a prefab document (`"prefab": true` root key),
    // used by the Content Browser to tell scenes from prefabs.
    static bool IsPrefabFile(const std::string &path);

    // --- Editor duplication ---
    // Clone `entity` (and its whole descendant subtree) into `scene` under
    // `parent` (nullptr = scene root). Fresh ids/uuid are assigned on every
    // clone — exactly like instantiating a prefab — while the original's
    // components, transforms, and relative child structure are preserved.
    // Returns the clone's root entity (nullptr only if the source is invalid).
    static Entity *DuplicateEntity(Scene &scene, const Entity &entity,
                                   Entity *parent = nullptr);

    // --- Undo / history support (Phase 22) ---
    // Capture an entity subtree as JSON (components + children, plus each
    // node's uuid) so a deleted entity can be restored byte-for-byte by the
    // editor's undo history.
    static json::Value SerializeEntityTree(const Entity &entity);
    // Re-spawn a captured entity subtree under `parent` (nullptr = scene
    // root), restoring the stored uuids. Returns the root entity.
    static Entity *SpawnEntityTree(Scene &scene, const json::Value &tree,
                                   Entity *parent = nullptr);
};
