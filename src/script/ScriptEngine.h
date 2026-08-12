#pragma once

#include <string>
#include <vector>

struct lua_State;
class Scene;
struct Entity;
class AudioManager;

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
    // alive. Runtime errors are recorded in LastError() and routed to the
    // engine console.
    void UpdateSession(Scene &scene, float dt);

    // Collision / trigger event kind. The PhysicsManager dispatches these to
    // scripted entities with a handle to the other entity of the pair.
    enum class ScriptEvent { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit };

    // Fire the matching hook (OnCollisionEnter/Exit or OnTriggerEnter/Exit)
    // on the script bound for `entity_id`, passing `other` as its argument.
    // No-op when the entity has no script or the hook is absent.
    void DispatchEvent(int entity_id, ScriptEvent event, Entity *other);

    // Tear the running session down and bind every scripted entity again,
    // re-reading each script file from disk and re-firing OnStart. Used by the
    // script editor's Save & Reload to apply edits to the live play session.
    // Returns false only if a fresh VM could not be created.
    bool ReloadSession(Scene &scene, std::string &errors);

    // Tear the VM down and release all per-entity references.
    void StopSession();

    // The AudioManager the Audio.* bindings route playback through. May be
    // null (audio is optional at runtime); the bindings then degrade to a
    // silent error-free no-op. The engine never owns it through this pointer.
    void SetAudioManager(AudioManager *audio);

    const std::string &LastError() const { return m_error; }

private:
    bool BindEntity(Scene &scene, Entity &entity, std::string &error);

    struct ScriptedEntity
    {
        int entity_id;
        int env_ref;        // registry ref to the script's _ENV table
        int on_start_ref;   // registry ref to OnStart (LUA_NOREF if absent)
        int on_update_ref;  // registry ref to OnUpdate (LUA_NOREF if absent)
        int on_collision_enter_ref;  // OnCollisionEnter(other)   (LUA_NOREF if absent)
        int on_collision_exit_ref;   // OnCollisionExit(other)    (LUA_NOREF if absent)
        int on_trigger_enter_ref;    // OnTriggerEnter(other)     (LUA_NOREF if absent)
        int on_trigger_exit_ref;     // OnTriggerExit(other)      (LUA_NOREF if absent)
    };

    lua_State *m_lua;
    std::vector<ScriptedEntity> m_scripted;
    std::string m_error;
    std::string m_last_error_logged;  // dedupe persistent runtime errors
    AudioManager *m_audio;
};
