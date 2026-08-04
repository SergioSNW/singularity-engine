#pragma once

#include <string>
#include <vector>

struct lua_State;
class Scene;
struct Entity;

// Embeds a Lua 5.4 runtime and binds the engine's core types (Vector3,
// Transform, Entity) so per-entity gameplay scripts can manipulate transforms
// during play mode. Each entity with a non-empty ScriptComponent::path gets its
// own Lua environment (`_ENV`) preloaded with `entity`, `transform`, and `self`
// globals; the chunk's OnStart() / OnUpdate(dt) functions drive the lifecycle.
//
// Sessions map 1:1 to play mode: StartSession() on EnterPlayMode binds every
// scripted entity and calls OnStart, UpdateSession() runs OnUpdate(dt) each
// frame while playing, and StopSession() on exit tears the VM down so no state
// leaks between runs.
class ScriptEngine
{
public:
    ScriptEngine();
    ~ScriptEngine();

    // Ensure the Lua VM exists, register the bindings, bind every scripted
    // entity in `scene`, and call each OnStart. Returns false only if the VM
    // could not be created; per-entity load errors accumulate in `errors`.
    bool StartSession(Scene &scene, std::string &errors);

    // Call OnUpdate(dt) on every bound entity (resolved by id) that is still
    // alive. Runtime errors are recorded in LastError() and printed to stderr.
    void UpdateSession(Scene &scene, float dt);

    // Tear the VM down and release all per-entity references.
    void StopSession();

    const std::string &LastError() const { return m_error; }

private:
    bool BindEntity(Scene &scene, Entity &entity, std::string &error);

    struct ScriptedEntity
    {
        int entity_id;
        int env_ref;        // registry ref to the script's _ENV table
        int on_start_ref;   // registry ref to OnStart (LUA_NOREF if absent)
        int on_update_ref;  // registry ref to OnUpdate (LUA_NOREF if absent)
    };

    lua_State *m_lua;
    std::vector<ScriptedEntity> m_scripted;
    std::string m_error;
};
