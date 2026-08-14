#include "script/ScriptEngine.h"

#include "Scene.h"
#include "Entity.h"
#include "AudioManager.h"
#include "core/Console.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// Lua userdata layout
//
// Vector3 is the only boxed value that is also returned by reference: a
// `transform.position` read pushes a *live view* pointing at the entity's
// float[3] so `t.position.x = 1` mutates the transform in place, while an
// assignment like `t.position = Vector3(1,2,3)` writes the whole vector back.
// Owned values (results of arithmetic, the Vector3 constructor) keep their
// data in `storage`.
// ---------------------------------------------------------------------------

struct LuaVec3
{
    float *ptr;       // live view into an Entity transform (3 contiguous floats)
    float storage[3]; // owned storage when ptr == nullptr
};

struct LuaTransform
{
    Entity *entity;
};

struct LuaEntity
{
    Entity *entity;
};

namespace {

const char *kVec3MT      = "Singe.Vector3";
const char *kTransformMT = "Singe.Transform";
const char *kEntityMT    = "Singe.Entity";
const char *kApiRegistry = "Singe.EngineApi";

// The AudioManager the Audio.* bindings route through. Held as a plain pointer
// (the engine owns the manager; this is only an observer for the Lua bridge).
AudioManager *g_audio_manager = nullptr;

// The Scene the REPL's `scene` table resolves against. Rebound on every
// Execute() call so snippets always address the active scene (and never a torn
// down play session); mirrors g_audio_manager's observer semantics.
Scene *g_exec_scene = nullptr;

// --- Vector3 ---

float *LuaVec3Ptr(LuaVec3 *v)
{
    return v->ptr ? v->ptr : v->storage;
}

LuaVec3 *CheckVec3(lua_State *L, int idx)
{
    return (LuaVec3 *)luaL_checkudata(L, idx, kVec3MT);
}

void PushVec3View(lua_State *L, float *view)
{
    LuaVec3 *v = (LuaVec3 *)lua_newuserdata(L, sizeof(LuaVec3));
    v->ptr = view;
    v->storage[0] = view[0];
    v->storage[1] = view[1];
    v->storage[2] = view[2];
    luaL_setmetatable(L, kVec3MT);
}

void PushVec3(lua_State *L, float x, float y, float z)
{
    LuaVec3 *v = (LuaVec3 *)lua_newuserdata(L, sizeof(LuaVec3));
    v->ptr = nullptr;
    v->storage[0] = x;
    v->storage[1] = y;
    v->storage[2] = z;
    luaL_setmetatable(L, kVec3MT);
}

// Accept a Vector3 userdata or a {x=,y=,z=} table and write its components.
void ParseVec3(lua_State *L, int idx, float out[3])
{
    if (lua_type(L, idx) == LUA_TUSERDATA)
    {
        LuaVec3 *v = CheckVec3(L, idx);
        float *p = LuaVec3Ptr(v);
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        return;
    }
    if (lua_type(L, idx) == LUA_TTABLE)
    {
        lua_getfield(L, idx, "x");
        out[0] = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
        lua_pop(L, 1);
        lua_getfield(L, idx, "y");
        out[1] = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
        lua_pop(L, 1);
        lua_getfield(L, idx, "z");
        out[2] = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
        lua_pop(L, 1);
        return;
    }
    luaL_error(L, "expected a Vector3 or {x,y,z} table, got %s",
               luaL_typename(L, idx));
}

int LuaVector3New(lua_State *L)
{
    int n = lua_gettop(L);
    if (n == 1 && lua_type(L, 1) == LUA_TUSERDATA)
    {
        LuaVec3 *src = CheckVec3(L, 1);
        float *p = LuaVec3Ptr(src);
        PushVec3(L, p[0], p[1], p[2]);
        return 1;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (n >= 1 && lua_isnumber(L, 1)) x = (float)lua_tonumber(L, 1);
    if (n >= 2 && lua_isnumber(L, 2)) y = (float)lua_tonumber(L, 2);
    if (n >= 3 && lua_isnumber(L, 3)) z = (float)lua_tonumber(L, 3);
    PushVec3(L, x, y, z);
    return 1;
}

int LuaVec3Index(lua_State *L)
{
    LuaVec3 *v = CheckVec3(L, 1);
    const char *key = luaL_checkstring(L, 2);
    float *p = LuaVec3Ptr(v);
    if (std::strcmp(key, "x") == 0) { lua_pushnumber(L, p[0]); return 1; }
    if (std::strcmp(key, "y") == 0) { lua_pushnumber(L, p[1]); return 1; }
    if (std::strcmp(key, "z") == 0) { lua_pushnumber(L, p[2]); return 1; }
    lua_getfield(L, lua_upvalueindex(1), key); // fall through to methods
    return 1;
}

int LuaVec3NewIndex(lua_State *L)
{
    LuaVec3 *v = CheckVec3(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!lua_isnumber(L, 3))
        return luaL_error(L, "vector field '%s' expects a number", key);
    float *p = LuaVec3Ptr(v);
    const float val = (float)lua_tonumber(L, 3);
    if (std::strcmp(key, "x") == 0) { p[0] = val; return 0; }
    if (std::strcmp(key, "y") == 0) { p[1] = val; return 0; }
    if (std::strcmp(key, "z") == 0) { p[2] = val; return 0; }
    return luaL_error(L, "vector field '%s' is read-only or unknown", key);
}

int LuaVec3Add(lua_State *L)
{
    LuaVec3 *a = CheckVec3(L, 1), *b = CheckVec3(L, 2);
    float *pa = LuaVec3Ptr(a), *pb = LuaVec3Ptr(b);
    PushVec3(L, pa[0] + pb[0], pa[1] + pb[1], pa[2] + pb[2]);
    return 1;
}

int LuaVec3Sub(lua_State *L)
{
    LuaVec3 *a = CheckVec3(L, 1), *b = CheckVec3(L, 2);
    float *pa = LuaVec3Ptr(a), *pb = LuaVec3Ptr(b);
    PushVec3(L, pa[0] - pb[0], pa[1] - pb[1], pa[2] - pb[2]);
    return 1;
}

int LuaVec3Mul(lua_State *L)
{
    if (lua_isnumber(L, 2))
    {
        LuaVec3 *a = CheckVec3(L, 1);
        float *p = LuaVec3Ptr(a);
        const float s = (float)lua_tonumber(L, 2);
        PushVec3(L, p[0] * s, p[1] * s, p[2] * s);
        return 1;
    }
    if (lua_isnumber(L, 1))
    {
        LuaVec3 *b = CheckVec3(L, 2);
        float *p = LuaVec3Ptr(b);
        const float s = (float)lua_tonumber(L, 1);
        PushVec3(L, p[0] * s, p[1] * s, p[2] * s);
        return 1;
    }
    LuaVec3 *a = CheckVec3(L, 1), *b = CheckVec3(L, 2);
    float *pa = LuaVec3Ptr(a), *pb = LuaVec3Ptr(b);
    PushVec3(L, pa[0] * pb[0], pa[1] * pb[1], pa[2] * pb[2]);
    return 1;
}

int LuaVec3Div(lua_State *L)
{
    LuaVec3 *a = CheckVec3(L, 1);
    const float s = (float)luaL_checknumber(L, 2);
    float *p = LuaVec3Ptr(a);
    PushVec3(L, p[0] / s, p[1] / s, p[2] / s);
    return 1;
}

int LuaVec3Unm(lua_State *L)
{
    LuaVec3 *a = CheckVec3(L, 1);
    float *p = LuaVec3Ptr(a);
    PushVec3(L, -p[0], -p[1], -p[2]);
    return 1;
}

int LuaVec3Eq(lua_State *L)
{
    LuaVec3 *a = CheckVec3(L, 1), *b = CheckVec3(L, 2);
    float *pa = LuaVec3Ptr(a), *pb = LuaVec3Ptr(b);
    lua_pushboolean(L,
        std::fabs(pa[0] - pb[0]) < 1e-5f &&
        std::fabs(pa[1] - pb[1]) < 1e-5f &&
        std::fabs(pa[2] - pb[2]) < 1e-5f);
    return 1;
}

int LuaVec3ToString(lua_State *L)
{
    LuaVec3 *v = CheckVec3(L, 1);
    float *p = LuaVec3Ptr(v);
    lua_pushfstring(L, "(%.3f, %.3f, %.3f)", (double)p[0], (double)p[1], (double)p[2]);
    return 1;
}

int LuaVec3Norm(lua_State *L)
{
    LuaVec3 *v = CheckVec3(L, 1);
    float *p = LuaVec3Ptr(v);
    const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (len < 1e-8f)
        PushVec3(L, 0.0f, 0.0f, 0.0f);
    else
        PushVec3(L, p[0] / len, p[1] / len, p[2] / len);
    return 1;
}

int LuaVec3Length(lua_State *L)
{
    LuaVec3 *v = CheckVec3(L, 1);
    float *p = LuaVec3Ptr(v);
    lua_pushnumber(L, std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]));
    return 1;
}

int LuaVec3Dot(lua_State *L)
{
    LuaVec3 *a = CheckVec3(L, 1), *b = CheckVec3(L, 2);
    float *pa = LuaVec3Ptr(a), *pb = LuaVec3Ptr(b);
    lua_pushnumber(L, pa[0] * pb[0] + pa[1] * pb[1] + pa[2] * pb[2]);
    return 1;
}

int LuaVec3Cross(lua_State *L)
{
    LuaVec3 *a = CheckVec3(L, 1), *b = CheckVec3(L, 2);
    float *pa = LuaVec3Ptr(a), *pb = LuaVec3Ptr(b);
    PushVec3(L,
        pa[1] * pb[2] - pa[2] * pb[1],
        pa[2] * pb[0] - pa[0] * pb[2],
        pa[0] * pb[1] - pa[1] * pb[0]);
    return 1;
}

void RegisterVec3(lua_State *L)
{
    luaL_newmetatable(L, kVec3MT);          // [mt]
    lua_newtable(L);                        // [mt, methods]

    lua_pushcfunction(L, LuaVec3Norm);   lua_setfield(L, -2, "norm");
    lua_pushcfunction(L, LuaVec3Length); lua_setfield(L, -2, "length");
    lua_pushcfunction(L, LuaVec3Dot);    lua_setfield(L, -2, "dot");
    lua_pushcfunction(L, LuaVec3Cross);  lua_setfield(L, -2, "cross");

    lua_pushvalue(L, -2);                   // [mt, methods, methods]
    lua_pushcclosure(L, LuaVec3Index, 1);   // [mt, methods, closure]
    lua_setfield(L, -3, "__index");         // mt.__index = closure -> [mt, methods]
    lua_pushcfunction(L, LuaVec3NewIndex);
    lua_setfield(L, -3, "__newindex");      // mt.__newindex = fn -> [mt, methods]
    lua_pushcfunction(L, LuaVec3Add);   lua_setfield(L, -3, "__add");
    lua_pushcfunction(L, LuaVec3Sub);   lua_setfield(L, -3, "__sub");
    lua_pushcfunction(L, LuaVec3Mul);   lua_setfield(L, -3, "__mul");
    lua_pushcfunction(L, LuaVec3Div);   lua_setfield(L, -3, "__div");
    lua_pushcfunction(L, LuaVec3Unm);   lua_setfield(L, -3, "__unm");
    lua_pushcfunction(L, LuaVec3Eq);    lua_setfield(L, -3, "__eq");
    lua_pushcfunction(L, LuaVec3ToString); lua_setfield(L, -3, "__tostring");

    lua_pop(L, 2);                      // []
}

// --- Transform ---

void PushTransform(lua_State *L, Entity *entity)
{
    LuaTransform *t = (LuaTransform *)lua_newuserdata(L, sizeof(LuaTransform));
    t->entity = entity;
    luaL_setmetatable(L, kTransformMT);
}

int LuaTransformIndex(lua_State *L)
{
    LuaTransform *t = (LuaTransform *)luaL_checkudata(L, 1, kTransformMT);
    const char *key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "position") == 0) { PushVec3View(L, t->entity->transform.position); return 1; }
    if (std::strcmp(key, "rotation") == 0) { PushVec3View(L, t->entity->transform.rotation); return 1; }
    if (std::strcmp(key, "scale")    == 0) { PushVec3View(L, t->entity->transform.scale);    return 1; }
    lua_getfield(L, lua_upvalueindex(1), key); // methods (reserved)
    return 1;
}

int LuaTransformNewIndex(lua_State *L)
{
    LuaTransform *t = (LuaTransform *)luaL_checkudata(L, 1, kTransformMT);
    const char *key = luaL_checkstring(L, 2);
    float v[3];
    ParseVec3(L, 3, v);
    if (std::strcmp(key, "position") == 0)
    {
        t->entity->transform.position[0] = v[0];
        t->entity->transform.position[1] = v[1];
        t->entity->transform.position[2] = v[2];
        return 0;
    }
    if (std::strcmp(key, "rotation") == 0)
    {
        t->entity->transform.rotation[0] = v[0];
        t->entity->transform.rotation[1] = v[1];
        t->entity->transform.rotation[2] = v[2];
        return 0;
    }
    if (std::strcmp(key, "scale") == 0)
    {
        t->entity->transform.scale[0] = v[0];
        t->entity->transform.scale[1] = v[1];
        t->entity->transform.scale[2] = v[2];
        return 0;
    }
    return luaL_error(L, "transform: '%s' is read-only", key);
}

void RegisterTransform(lua_State *L)
{
    luaL_newmetatable(L, kTransformMT);     // [mt]
    lua_newtable(L);                        // [mt, methods]
    lua_pushvalue(L, -2);                   // [mt, methods, methods]
    lua_pushcclosure(L, LuaTransformIndex, 1);  // [mt, methods, closure]
    lua_setfield(L, -3, "__index");         // mt.__index = closure -> [mt, methods]
    lua_pushcfunction(L, LuaTransformNewIndex);
    lua_setfield(L, -3, "__newindex");      // mt.__newindex = fn -> [mt, methods]
    lua_pop(L, 2);                          // []
}

// --- Entity ---

void PushEntity(lua_State *L, Entity *entity)
{
    LuaEntity *e = (LuaEntity *)lua_newuserdata(L, sizeof(LuaEntity));
    e->entity = entity;
    luaL_setmetatable(L, kEntityMT);
}

int LuaEntityIndex(lua_State *L)
{
    LuaEntity *e = (LuaEntity *)luaL_checkudata(L, 1, kEntityMT);
    const char *key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "name") == 0) { lua_pushstring(L, e->entity->tag.tag.c_str()); return 1; }
    if (std::strcmp(key, "id") == 0)   { lua_pushinteger(L, e->entity->id); return 1; }
    if (std::strcmp(key, "transform") == 0) { PushTransform(L, e->entity); return 1; }
    lua_getfield(L, lua_upvalueindex(1), key); // methods (reserved)
    return 1;
}

int LuaEntityNewIndex(lua_State *L)
{
    LuaEntity *e = (LuaEntity *)luaL_checkudata(L, 1, kEntityMT);
    const char *key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "name") == 0)
    {
        e->entity->tag.tag = luaL_checkstring(L, 3);
        return 0;
    }
    return luaL_error(L, "entity: '%s' is read-only", key);
}

// --- print() ---
//
// The stdlib print writes straight to stdout, which is meaningless once the
// editor owns its own console. The global is replaced with this handler so
// Lua output funnels into the Console sink as Info rows (tab-separated, one
// tostring() per argument, mirroring the stdlib semantics).
int LuaPrint(lua_State *L)
{
    const int n = lua_gettop(L);
    std::string out;
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; ++i)
    {
        if (i > 1)
            out += '\t';
        lua_pushvalue(L, -1);           // [.., tostring, arg]
        lua_pushvalue(L, i);
        if (lua_pcall(L, 1, 1, 0) == LUA_OK)
        {
            const char *s = lua_tostring(L, -1);
            if (s)
                out += s;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);                      // pop tostring
    ConsoleInfo(out);
    return 0;
}

// --- Audio ---
//
// Gameplay sound bridge: scripts call Audio.Play(path, volume?, loop?) to fire
// a one-shot / looping sample and Audio.Stop(path) to halt every channel
// playing it. Paths are the same asset references the AudioComponent uses
// (e.g. "assets/audio/beep.wav"). When no AudioManager is attached (headless /
// audio disabled) both calls degrade to a silent no-op.

int LuaAudioPlay(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    const float volume = (float)luaL_optnumber(L, 2, 1.0);
    const bool loop = lua_toboolean(L, 3);
    if (!g_audio_manager)
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    lua_pushinteger(L, g_audio_manager->Play(path, volume, loop));
    return 1;
}

int LuaAudioStop(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    if (g_audio_manager)
        g_audio_manager->Stop(path);
    return 0;
}

void RegisterEntity(lua_State *L)
{
    luaL_newmetatable(L, kEntityMT);        // [mt]
    lua_newtable(L);                        // [mt, methods]
    lua_pushvalue(L, -2);                   // [mt, methods, methods]
    lua_pushcclosure(L, LuaEntityIndex, 1); // [mt, methods, closure]
    lua_setfield(L, -3, "__index");         // mt.__index = closure -> [mt, methods]
    lua_pushcfunction(L, LuaEntityNewIndex);
    lua_setfield(L, -3, "__newindex");      // mt.__newindex = fn -> [mt, methods]
    lua_pop(L, 2);                          // []
}

// --- Engine API table (chained before _G in every script's _ENV) ---

void RegisterEngineApi(lua_State *L)
{
    lua_newtable(L);                        // [api]
    lua_pushcfunction(L, LuaVector3New); lua_setfield(L, -2, "Vector3");
    lua_pushcfunction(L, LuaVector3New); lua_setfield(L, -2, "Vec3");

    lua_newtable(L);                        // [api, audio]
    lua_pushcfunction(L, LuaAudioPlay); lua_setfield(L, -2, "Play");
    lua_pushcfunction(L, LuaAudioStop); lua_setfield(L, -2, "Stop");
    lua_setfield(L, -2, "Audio");           // api.Audio = audio -> [api]

    lua_newtable(L);                        // [api, api_mt]
    lua_getglobal(L, "_G");
    lua_setfield(L, -2, "__index");         // api_mt.__index = _G
    lua_setmetatable(L, -2);                // setmetatable(api, api_mt) -> [api]
    lua_setfield(L, LUA_REGISTRYINDEX, kApiRegistry);
}

// --- REPL `scene` table ---
//
// The persistent REPL scratchpad exposes the active scene through a small
// read/write surface: count() sizes the entity list, get(i) fetches the i-th
// entity (1-based), find(name) looks one up by tag, and name reports the
// scene's display name. Entities are the same Singe.Entity userdata gameplay
// scripts use, so snippets can read and mutate transforms in place.

int LuaSceneName(lua_State *L)
{
    lua_pushstring(L, (g_exec_scene && !g_exec_scene->Meta().name.empty())
                          ? g_exec_scene->Meta().name.c_str()
                          : "");
    return 1;
}

int LuaSceneCount(lua_State *L)
{
    lua_pushinteger(L, g_exec_scene ? (lua_Integer)g_exec_scene->GetEntities().size() : 0);
    return 1;
}

int LuaSceneGet(lua_State *L)
{
    const lua_Integer idx = luaL_checkinteger(L, 1);
    if (!g_exec_scene)
    {
        lua_pushnil(L);
        return 1;
    }
    auto &entities = g_exec_scene->GetEntities();
    if (idx < 1 || idx > (lua_Integer)entities.size())
    {
        lua_pushnil(L);
        return 1;
    }
    PushEntity(L, entities[(std::size_t)(idx - 1)].get());
    return 1;
}

int LuaSceneFind(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    if (!g_exec_scene)
    {
        lua_pushnil(L);
        return 1;
    }
    for (auto &entity_ptr : g_exec_scene->GetEntities())
    {
        if (entity_ptr->tag.tag == name)
        {
            PushEntity(L, entity_ptr.get());
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// Build a fresh _ENV table for one entity: its own metatable resolves unknown
// names through the engine API table (which itself chains to Lua's _G), so
// scripts see `entity`, `transform`, `self`, `Vector3`, and the stdlib.
void NewScriptEnv(lua_State *L, Entity *entity)
{
    lua_newtable(L);                        // [env]
    lua_newtable(L);                        // [env, mt]
    lua_getfield(L, LUA_REGISTRYINDEX, kApiRegistry);
    lua_setfield(L, -2, "__index");         // mt.__index = api
    lua_setmetatable(L, -2);                // setmetatable(env, mt) -> [env]

    lua_pushvalue(L, -1);                   // [env, env]
    lua_setfield(L, -2, "self");            // env.self = env
    PushEntity(L, entity);                  // [env, entity]
    lua_setfield(L, -2, "entity");
    PushTransform(L, entity);               // [env, transform]
    lua_setfield(L, -2, "transform");
}

} // namespace

// ---------------------------------------------------------------------------
// ScriptEngine
// ---------------------------------------------------------------------------

ScriptEngine::ScriptEngine()
    : m_lua(nullptr)
    , m_audio(nullptr)
    , m_repl(nullptr)
    , m_repl_env_ref(LUA_NOREF)
{
}

ScriptEngine::~ScriptEngine()
{
    StopSession();
    if (m_repl)
    {
        luaL_unref(m_repl, LUA_REGISTRYINDEX, m_repl_env_ref);
        lua_close(m_repl);
        m_repl = nullptr;
    }
}

void ScriptEngine::SetAudioManager(AudioManager *audio)
{
    m_audio = audio;
    g_audio_manager = audio;
}

bool ScriptEngine::BindEntity(Scene &scene, Entity &entity, std::string &error)
{
    (void)scene;
    lua_State *L = m_lua;

    std::ifstream in(entity.script.path, std::ios::in | std::ios::binary);
    if (!in.is_open())
    {
        error = "cannot open script '" + entity.script.path + "'";
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string code = buffer.str();

    NewScriptEnv(L, &entity);               // [env]
    if (luaL_loadbuffer(L, code.data(), code.size(), entity.script.path.c_str()) != LUA_OK)
    {
        error = lua_tostring(L, -1);
        lua_pop(L, 2);                      // pop error + env
        return false;
    }

    lua_pushvalue(L, -2);                   // [env, chunk, env]
    lua_setupvalue(L, -2, 1);               // chunk's _ENV = env -> [env, chunk]
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
        error = lua_tostring(L, -1);
        lua_pop(L, 2);                      // pop error + env
        return false;
    }
    // stack: [env]

    ScriptedEntity se;
    se.entity_id = entity.id;
    // A hook is captured as a registry ref when the script defines it;
    // otherwise it stays LUA_NOREF and DispatchEvent skips the call.
    auto bind_hook = [L](const char *name) -> int {
        lua_getfield(L, -1, name);
        if (lua_isfunction(L, -1))
            return luaL_ref(L, LUA_REGISTRYINDEX);
        lua_pop(L, 1);
        return LUA_NOREF;
    };
    se.on_start_ref = bind_hook("OnStart");
    se.on_update_ref = bind_hook("OnUpdate");
    se.on_collision_enter_ref = bind_hook("OnCollisionEnter");
    se.on_collision_exit_ref = bind_hook("OnCollisionExit");
    se.on_trigger_enter_ref = bind_hook("OnTriggerEnter");
    se.on_trigger_exit_ref = bind_hook("OnTriggerExit");
    se.env_ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops env

    m_scripted.push_back(se);

    if (se.on_start_ref != LUA_NOREF)
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, se.on_start_ref);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
        {
            error = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    }
    return true;
}

bool ScriptEngine::StartSession(Scene &scene, std::string &errors)
{
    StopSession();                          // no-op when not running

    m_lua = luaL_newstate();
    if (!m_lua)
    {
        errors = "failed to create Lua state";
        return false;
    }
    luaL_openlibs(m_lua);
    RegisterVec3(m_lua);
    RegisterTransform(m_lua);
    RegisterEntity(m_lua);
    RegisterEngineApi(m_lua);

    // Route print() into the editor console instead of stdout. Scripts resolve
    // the name through their _ENV -> engine API -> _G chain, so overriding the
    // _G global reaches every scripted environment.
    lua_pushcfunction(m_lua, LuaPrint);
    lua_setglobal(m_lua, "print");

    errors.clear();
    for (auto &entity_ptr : scene.GetEntities())
    {
        Entity &entity = *entity_ptr;
        if (entity.script.path.empty())
            continue;
        std::string error;
        BindEntity(scene, entity, error);
        if (!error.empty())
        {
            if (!errors.empty())
                errors += "; ";
            errors += "[" + entity.tag.tag + "] " + error;
            ConsoleError("[ScriptEngine] [" + entity.tag.tag + "] " + error);
        }
    }

    m_error = errors;
    m_last_error_logged.clear();
    return true;
}

void ScriptEngine::UpdateSession(Scene &scene, float dt)
{
    if (!m_lua)
        return;

    m_error.clear();
    for (const ScriptedEntity &se : m_scripted)
    {
        if (se.on_update_ref == LUA_NOREF)
            continue;
        if (!scene.GetEntityById(se.entity_id))
            continue;                       // destroyed during play: skip

        lua_rawgeti(m_lua, LUA_REGISTRYINDEX, se.on_update_ref);
        lua_pushnumber(m_lua, (lua_Number)dt);
        if (lua_pcall(m_lua, 1, 0, 0) != LUA_OK)
        {
            if (m_error.empty())
                m_error = lua_tostring(m_lua, -1);
            lua_pop(m_lua, 1);
        }
    }

    if (!m_error.empty() && m_error != m_last_error_logged)
    {
        // A runtime exception persists across frames; report it once instead of
        // spamming the console at 60 fps.
        m_last_error_logged = m_error;
        ConsoleError("[ScriptEngine] " + m_error);
    }
}

void ScriptEngine::DispatchEvent(int entity_id, ScriptEvent event, Entity *other)
{
    if (!m_lua)
        return;

    for (const ScriptedEntity &se : m_scripted)
    {
        if (se.entity_id != entity_id)
            continue;

        int ref = LUA_NOREF;
        switch (event)
        {
            case ScriptEvent::CollisionEnter: ref = se.on_collision_enter_ref; break;
            case ScriptEvent::CollisionExit:  ref = se.on_collision_exit_ref;  break;
            case ScriptEvent::TriggerEnter:   ref = se.on_trigger_enter_ref;   break;
            case ScriptEvent::TriggerExit:    ref = se.on_trigger_exit_ref;    break;
        }
        if (ref == LUA_NOREF)
            return;                       // hook not defined by this script

        lua_rawgeti(m_lua, LUA_REGISTRYINDEX, ref);
        if (other)
            PushEntity(m_lua, other);
        else
            lua_pushnil(m_lua);
        if (lua_pcall(m_lua, 1, 0, 0) != LUA_OK)
        {
            if (m_error.empty())
                m_error = lua_tostring(m_lua, -1);
            lua_pop(m_lua, 1);
        }
        if (!m_error.empty() && m_error != m_last_error_logged)
        {
            m_last_error_logged = m_error;
            ConsoleError("[ScriptEngine] " + m_error);
        }
        return;
    }
}

bool ScriptEngine::ReloadSession(Scene &scene, std::string &errors)
{
    StopSession();                      // releases refs + VM from the old run
    return StartSession(scene, errors); // re-reads every script file from disk
}

void ScriptEngine::StopSession()
{
    if (!m_lua)
        return;

    for (const ScriptedEntity &se : m_scripted)
    {
        luaL_unref(m_lua, LUA_REGISTRYINDEX, se.env_ref);
        if (se.on_start_ref != LUA_NOREF)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, se.on_start_ref);
        if (se.on_update_ref != LUA_NOREF)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, se.on_update_ref);
        if (se.on_collision_enter_ref != LUA_NOREF)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, se.on_collision_enter_ref);
        if (se.on_collision_exit_ref != LUA_NOREF)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, se.on_collision_exit_ref);
        if (se.on_trigger_enter_ref != LUA_NOREF)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, se.on_trigger_enter_ref);
        if (se.on_trigger_exit_ref != LUA_NOREF)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, se.on_trigger_exit_ref);
    }
    m_scripted.clear();

    lua_close(m_lua);
    m_lua = nullptr;
    m_error.clear();
    m_last_error_logged.clear();
}

void ScriptEngine::EnsureReplState()
{
    if (m_repl)
        return;

    m_repl = luaL_newstate();
    if (!m_repl)
        return;
    luaL_openlibs(m_repl);
    RegisterVec3(m_repl);
    RegisterTransform(m_repl);
    RegisterEntity(m_repl);
    RegisterEngineApi(m_repl);

    // Route print() into the editor console, exactly like a play session.
    lua_pushcfunction(m_repl, LuaPrint);
    lua_setglobal(m_repl, "print");

    // Isolated scratchpad environment: a fresh table chained to the engine API
    // (which itself chains to _G), so Vector3 / Audio / print / the stdlib
    // resolve without polluting the globals of any play session. The `scene`
    // table is bound to whichever scene Execute() was last handed.
    lua_newtable(m_repl);                       // [env]
    lua_newtable(m_repl);                       // [env, mt]
    lua_getfield(m_repl, LUA_REGISTRYINDEX, kApiRegistry);
    lua_setfield(m_repl, -2, "__index");
    lua_setmetatable(m_repl, -2);               // [env]

    lua_newtable(m_repl);                       // [env, scene]
    lua_pushcfunction(m_repl, LuaSceneName);  lua_setfield(m_repl, -2, "name");
    lua_pushcfunction(m_repl, LuaSceneCount); lua_setfield(m_repl, -2, "count");
    lua_pushcfunction(m_repl, LuaSceneGet);   lua_setfield(m_repl, -2, "get");
    lua_pushcfunction(m_repl, LuaSceneFind);  lua_setfield(m_repl, -2, "find");
    lua_setfield(m_repl, -2, "scene");          // env.scene = scene -> [env]

    m_repl_env_ref = luaL_ref(m_repl, LUA_REGISTRYINDEX);
}

bool ScriptEngine::Execute(Scene &scene, const std::string &code, std::string &error)
{
    EnsureReplState();
    if (!m_repl)
    {
        error = "REPL unavailable (failed to create Lua state)";
        return false;
    }
    g_exec_scene = &scene;

    // [env]
    lua_rawgeti(m_repl, LUA_REGISTRYINDEX, m_repl_env_ref);
    if (luaL_loadbuffer(m_repl, code.data(), code.size(), "repl") != LUA_OK)
    {
        error = lua_tostring(m_repl, -1);
        lua_pop(m_repl, 2);                     // error + env
        ConsoleError("[REPL] " + error);
        return false;
    }
    lua_pushvalue(m_repl, -2);                  // [env, chunk, env]
    lua_setupvalue(m_repl, -2, 1);              // chunk's _ENV = env -> [env, chunk]
    if (lua_pcall(m_repl, 0, LUA_MULTRET, 0) != LUA_OK)
    {
        error = lua_tostring(m_repl, -1);
        lua_pop(m_repl, 2);                     // error + env
        ConsoleError("[REPL] " + error);
        return false;
    }
    // Success: stack is [env, r1..rn]. Print any chunk return values through
    // the same tostring path print() uses, so `return expr` echoes a value.
    const int n = lua_gettop(m_repl) - 1;
    if (n > 0)
    {
        lua_getglobal(m_repl, "tostring");      // [env, r1..rn, tostring]
        std::string out;
        for (int i = 1; i <= n; ++i)
        {
            if (i > 1)
                out += '\t';
            lua_pushvalue(m_repl, -1);          // tostring
            lua_pushvalue(m_repl, i + 1);       // r_i
            if (lua_pcall(m_repl, 1, 1, 0) == LUA_OK)
            {
                const char *s = lua_tostring(m_repl, -1);
                if (s)
                    out += s;
            }
            lua_pop(m_repl, 1);                 // pop result
        }
        lua_pop(m_repl, 1);                     // pop tostring
        ConsoleInfo("=> " + out);
    }
    lua_settop(m_repl, 0);                      // env + returns
    return true;
}
