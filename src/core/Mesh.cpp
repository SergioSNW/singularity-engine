#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

// --- builtin unit cube (matches the historical procedural primitive) ---

static const Vec3 CUBE_CORNERS[8] = {
    { -0.5f, -0.5f, -0.5f },  // 0
    {  0.5f, -0.5f, -0.5f },  // 1
    {  0.5f,  0.5f, -0.5f },  // 2
    { -0.5f,  0.5f, -0.5f },  // 3
    { -0.5f, -0.5f,  0.5f },  // 4
    {  0.5f, -0.5f,  0.5f },  // 5
    {  0.5f,  0.5f,  0.5f },  // 6
    { -0.5f,  0.5f,  0.5f },  // 7
};

// Per-face UVs map a full texture quad (u,v in [0,1], v flipped to SDL's
// top-left origin) onto every cube face, matching the unit-quad convention
// used by the OBJ loader's "vt" records.
static const Vec2 CUBE_FACE_UVS[6][4] = {
    { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } },
    { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } },
    { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } },
    { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } },
    { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } },
    { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } },
};

static const int CUBE_EDGES[12][2] = {
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
    { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
};

static const int CUBE_FACES[6][4] = {
    { 4, 5, 6, 7 },  // front  (z = +0.5)
    { 1, 0, 3, 2 },  // back   (z = -0.5)
    { 2, 3, 7, 6 },  // top    (y = +0.5)
    { 1, 5, 4, 0 },  // bottom (y = -0.5)
    { 5, 1, 2, 6 },  // right  (x = +0.5)
    { 0, 4, 7, 3 },  // left   (x = -0.5)
};

static Mesh BuildCubeMesh()
{
    Mesh mesh;
    mesh.name = "Cube Primitive";
    mesh.positions.reserve(36);
    mesh.uvs.reserve(36);
    for (int f = 0; f < 6; ++f)
    {
        const int *idx = CUBE_FACES[f];
        const Vec3 &a = CUBE_CORNERS[idx[0]];
        const Vec3 &b = CUBE_CORNERS[idx[1]];
        const Vec3 &c = CUBE_CORNERS[idx[2]];
        const Vec3 &d = CUBE_CORNERS[idx[3]];
        mesh.positions.push_back(a);
        mesh.positions.push_back(b);
        mesh.positions.push_back(c);
        mesh.positions.push_back(a);
        mesh.positions.push_back(c);
        mesh.positions.push_back(d);
        const Vec2 &t0 = CUBE_FACE_UVS[f][0];
        const Vec2 &t1 = CUBE_FACE_UVS[f][1];
        const Vec2 &t2 = CUBE_FACE_UVS[f][2];
        const Vec2 &t3 = CUBE_FACE_UVS[f][3];
        mesh.uvs.push_back(t0);
        mesh.uvs.push_back(t1);
        mesh.uvs.push_back(t2);
        mesh.uvs.push_back(t0);
        mesh.uvs.push_back(t2);
        mesh.uvs.push_back(t3);
    }
    mesh.edge_lines.reserve(24);
    for (int i = 0; i < 12; ++i)
    {
        mesh.edge_lines.push_back(CUBE_CORNERS[CUBE_EDGES[i][0]]);
        mesh.edge_lines.push_back(CUBE_CORNERS[CUBE_EDGES[i][1]]);
    }
    mesh.bounds_min = { -0.5f, -0.5f, -0.5f };
    mesh.bounds_max = {  0.5f,  0.5f,  0.5f };
    return mesh;
}

// --- built-in structural primitives (Wall / Floor / Ramp) ---
//
// Wall and Floor reuse the cube's face/edge topology (CUBE_FACES/CUBE_EDGES
// index into *any* 8-corner box with the same corner layout: 0-3 at the min-Z
// face going around low-Y/high-Y, 4-7 the same at max-Z), just with per-axis
// half-extents instead of a fixed 0.5, and the vertical range pivoted at the
// *base* (y0 = 0) rather than centered like the cube. That base pivot is
// deliberate: these are meant to be placed directly onto a landscape hit
// point (Application::ComputeDropWorldPos), and a center-pivoted box would
// spawn half-buried in the surface instead of sitting on top of it.
static Mesh BuildBoxMesh(const char *name, float half_x, float y0, float y1, float half_z)
{
    const Vec3 corners[8] = {
        { -half_x, y0, -half_z },  // 0
        {  half_x, y0, -half_z },  // 1
        {  half_x, y1, -half_z },  // 2
        { -half_x, y1, -half_z },  // 3
        { -half_x, y0,  half_z },  // 4
        {  half_x, y0,  half_z },  // 5
        {  half_x, y1,  half_z },  // 6
        { -half_x, y1,  half_z },  // 7
    };

    Mesh mesh;
    mesh.name = name;
    mesh.positions.reserve(36);
    mesh.uvs.reserve(36);
    for (int f = 0; f < 6; ++f)
    {
        const int *idx = CUBE_FACES[f];
        const Vec3 &a = corners[idx[0]];
        const Vec3 &b = corners[idx[1]];
        const Vec3 &c = corners[idx[2]];
        const Vec3 &d = corners[idx[3]];
        mesh.positions.push_back(a);
        mesh.positions.push_back(b);
        mesh.positions.push_back(c);
        mesh.positions.push_back(a);
        mesh.positions.push_back(c);
        mesh.positions.push_back(d);
        const Vec2 &t0 = CUBE_FACE_UVS[f][0];
        const Vec2 &t1 = CUBE_FACE_UVS[f][1];
        const Vec2 &t2 = CUBE_FACE_UVS[f][2];
        const Vec2 &t3 = CUBE_FACE_UVS[f][3];
        mesh.uvs.push_back(t0);
        mesh.uvs.push_back(t1);
        mesh.uvs.push_back(t2);
        mesh.uvs.push_back(t0);
        mesh.uvs.push_back(t2);
        mesh.uvs.push_back(t3);
    }
    mesh.edge_lines.reserve(24);
    for (int i = 0; i < 12; ++i)
    {
        mesh.edge_lines.push_back(corners[CUBE_EDGES[i][0]]);
        mesh.edge_lines.push_back(corners[CUBE_EDGES[i][1]]);
    }
    mesh.bounds_min = { -half_x, y0, -half_z };
    mesh.bounds_max = {  half_x, y1,  half_z };
    return mesh;
}

// A right-triangle wedge: flat base at y=0, rising from y=0 at x=-half_x to
// y=height at x=+half_x, constant across the full z extent. Base-pivoted
// like the wall/floor, for the same "sits on the placement point" reason.
//
// Six corners (a triangular prism: two mirrored triangle cross-sections
// extruded along Z) and five faces -- bottom, the vertical high-end face, the
// sloped ramp surface, and two triangular end caps. Every face's winding was
// verified by hand (cross(b-a, c-a) checked against the expected outward
// direction for each face) rather than assumed, since a wedge has no
// symmetry to lean on the way the box corners do.
static Mesh BuildRampMesh(float half_x, float height, float half_z)
{
    const Vec3 p0{ -half_x, 0.0f,      -half_z };  // low end, back
    const Vec3 p1{  half_x, 0.0f,      -half_z };  // base under high end, back
    const Vec3 p2{  half_x, height,    -half_z };  // top of high end, back
    const Vec3 p3{ -half_x, 0.0f,       half_z };  // low end, front
    const Vec3 p4{  half_x, 0.0f,       half_z };  // base under high end, front
    const Vec3 p5{  half_x, height,     half_z };  // top of high end, front

    Mesh mesh;
    mesh.name = "Ramp Primitive";
    mesh.positions.reserve(24);
    mesh.uvs.reserve(24);

    auto quad = [&](const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d) {
        mesh.positions.push_back(a); mesh.positions.push_back(b); mesh.positions.push_back(c);
        mesh.positions.push_back(a); mesh.positions.push_back(c); mesh.positions.push_back(d);
        mesh.uvs.push_back({0.0f, 1.0f}); mesh.uvs.push_back({1.0f, 1.0f}); mesh.uvs.push_back({1.0f, 0.0f});
        mesh.uvs.push_back({0.0f, 1.0f}); mesh.uvs.push_back({1.0f, 0.0f}); mesh.uvs.push_back({0.0f, 0.0f});
    };
    auto tri = [&](const Vec3 &a, const Vec3 &b, const Vec3 &c) {
        mesh.positions.push_back(a); mesh.positions.push_back(b); mesh.positions.push_back(c);
        mesh.uvs.push_back({0.0f, 0.0f}); mesh.uvs.push_back({1.0f, 0.0f}); mesh.uvs.push_back({0.5f, 1.0f});
    };

    quad(p0, p1, p4, p3);   // bottom            (outward -Y)
    quad(p1, p2, p5, p4);   // high-end vertical  (outward +X)
    quad(p0, p3, p5, p2);   // slope              (outward up + toward -X)
    tri(p0, p2, p1);        // back cap, z=-half_z (outward -Z)
    tri(p3, p4, p5);        // front cap, z=+half_z (outward +Z)

    mesh.edge_lines.reserve(18);
    const Vec3 edges[9][2] = {
        {p0,p1},{p1,p2},{p2,p0}, {p3,p4},{p4,p5},{p5,p3}, {p0,p3},{p1,p4},{p2,p5},
    };
    for (const auto &e : edges)
    {
        mesh.edge_lines.push_back(e[0]);
        mesh.edge_lines.push_back(e[1]);
    }

    mesh.bounds_min = { -half_x, 0.0f,   -half_z };
    mesh.bounds_max = {  half_x, height,  half_z };
    return mesh;
}

const char *const kBuiltinCubePath  = "__builtin_cube__";
const char *const kBuiltinWallPath  = "__builtin_wall__";
const char *const kBuiltinFloorPath = "__builtin_floor__";
const char *const kBuiltinRampPath  = "__builtin_ramp__";

const char *BuiltinPrimitiveDisplayName(const std::string &path)
{
    if (path == kBuiltinCubePath)  return "Cube";
    if (path == kBuiltinWallPath)  return "Wall";
    if (path == kBuiltinFloorPath) return "Floor";
    if (path == kBuiltinRampPath)  return "Ramp";
    return nullptr;
}

// --- .obj parsing ---

namespace {

struct EdgeKey
{
    unsigned a, b;
    bool operator<(const EdgeKey &o) const
    {
        if (a != o.a) return a < o.a;
        return b < o.b;
    }
};

std::string TrimLeft(const std::string &s)
{
    size_t i = s.find_first_not_of(" \t\r\n");
    return (i == std::string::npos) ? std::string() : s.substr(i);
}

// Parse one face vertex token ("1", "1/2", "1/2/3", "1//3", or a negative
// relative index). Returns true and 0-based indices into `verts`/`uvs` on
// success; `out_uv` is set to (unsigned)-1 when the token has no "vt" part.
bool ParseFaceIndex(const std::string &tok, const std::vector<Vec3> &verts,
                    const std::vector<Vec2> &uvs, unsigned &out_pos, unsigned &out_uv)
{
    size_t slash = tok.find('/');
    std::string num = (slash == std::string::npos) ? tok : tok.substr(0, slash);
    if (num.empty() || num == "-")
        return false;
    long idx = 0;
    for (char ch : num)
    {
        if (ch == '-') continue;
        if (ch < '0' || ch > '9')
            return false;
    }
    try { idx = std::stol(num); } catch (...) { return false; }

    if (idx < 0)
        idx = (long)verts.size() + idx + 1; // negative: 1-based from end
    if (idx < 1 || idx > (long)verts.size())
        return false;
    out_pos = (unsigned)(idx - 1);

    out_uv = (unsigned)-1;
    if (slash != std::string::npos && slash + 1 < tok.size())
    {
        size_t end = tok.find('/', slash + 1);
        std::string vt = tok.substr(slash + 1, end - (slash + 1));
        if (!vt.empty() && vt != "-")
        {
            long u = 0;
            try { u = std::stol(vt); } catch (...) { return false; }
            if (u < 0) u = (long)uvs.size() + u + 1;
            if (u >= 1 && u <= (long)uvs.size())
                out_uv = (unsigned)(u - 1);
        }
    }
    return true;
}

bool LoadObj(const std::string &path, Mesh &mesh, std::string *error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        if (error) *error = "cannot open mesh file '" + path + "'";
        return false;
    }

    std::vector<Vec3> verts;
    verts.reserve(512);
    std::vector<Vec2> uvs;
    uvs.reserve(256);
    std::vector<std::vector<unsigned>> faces;   // pairs: [pos, uv]
    faces.reserve(1024);

    std::string line;
    while (std::getline(in, line))
    {
        std::string t = TrimLeft(line);
        if (t.empty() || t[0] == '#')
            continue;
        std::istringstream ss(t);
        std::string kw;
        ss >> kw;
        if (kw == "v")
        {
            float x, y, z;
            if (ss >> x >> y >> z)
                verts.push_back({ x, y, z });
        }
        else if (kw == "vt")
        {
            float u, v;
            if (ss >> u >> v)
                uvs.push_back({ u, 1.0f - v }); // flip: OBJ v is bottom-up
        }
        else if (kw == "f")
        {
            std::vector<unsigned> face;
            std::string tok;
            while (ss >> tok)
            {
                unsigned pos, uv;
                if (ParseFaceIndex(tok, verts, uvs, pos, uv))
                {
                    face.push_back(pos);
                    face.push_back(uv);
                }
            }
            if (face.size() >= 6)
                faces.push_back(std::move(face));
        }
        // "vn", "vp", "g", "o", "s", "mtllib", "usemtl" are ignored.
    }

    if (verts.empty() || faces.empty())
    {
        if (error) *error = "mesh file '" + path + "' contains no renderable geometry";
        return false;
    }

    mesh.positions.clear();
    mesh.edge_lines.clear();
    mesh.uvs.clear();
    mesh.positions.reserve(faces.size() * 3);

    // A triangle carries UVs only when every referenced corner has one; a
    // single missing "vt" makes the whole mesh fall back to flat shading.
    bool any_uv = true;
    for (const auto &face : faces)
    {
        for (size_t i = 0; i < face.size(); i += 2)
            if (face[i + 1] == (unsigned)-1) { any_uv = false; break; }
        if (!any_uv) break;
    }
    if (any_uv)
        mesh.uvs.reserve(faces.size() * 3);

    std::set<EdgeKey> edge_set;
    for (const auto &face : faces)
    {
        size_t count = face.size() / 2;
        // Unique edge set for the wireframe pass (dedupes shared edges).
        for (size_t i = 0; i < count; ++i)
        {
            unsigned a = face[i * 2];
            unsigned b = face[((i + 1) % count) * 2];
            if (a == b) continue;
            edge_set.insert({ std::min(a, b), std::max(a, b) });
        }
        // Fan triangulation of the (possibly convex) polygon.
        for (size_t i = 1; i + 1 < count; ++i)
        {
            mesh.positions.push_back(verts[face[0]]);
            mesh.positions.push_back(verts[face[i * 2]]);
            mesh.positions.push_back(verts[face[(i + 1) * 2]]);
            if (any_uv)
            {
                mesh.uvs.push_back(uvs[face[1]]);
                mesh.uvs.push_back(uvs[face[i * 2 + 1]]);
                mesh.uvs.push_back(uvs[face[(i + 1) * 2 + 1]]);
            }
        }
    }
    for (const EdgeKey &k : edge_set)
    {
        mesh.edge_lines.push_back(verts[k.a]);
        mesh.edge_lines.push_back(verts[k.b]);
    }

    Vec3 mn = verts[0];
    Vec3 mx = verts[0];
    for (const Vec3 &v : verts)
    {
        mn.x = std::min(mn.x, v.x);  mn.y = std::min(mn.y, v.y);  mn.z = std::min(mn.z, v.z);
        mx.x = std::max(mx.x, v.x);  mx.y = std::max(mx.y, v.y);  mx.z = std::max(mx.z, v.z);
    }
    mesh.bounds_min = mn;
    mesh.bounds_max = mx;

    if (error) error->clear();
    return true;
}

bool FileExists(const std::string &path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

} // namespace

// Compute averaged vertex normals for Gouraud shading. For each triangle, the
// face normal is accumulated at all three vertex positions. Vertices at
// coincident positions (quantized to 4 decimal places) share the average,
// producing smooth shading across curved surfaces while preserving hard edges
// at vertices where faces meet at sharp angles.
static void ComputeVertexNormals(Mesh &mesh)
{
    mesh.normals.assign(mesh.positions.size(), { 0.0f, 0.0f, 0.0f });
    if (mesh.positions.size() < 3)
        return;

    // Map quantized position → accumulated normal. The key is a snapped int3
    // so vertices at the same position (within 0.0001 units) share normals.
    struct PosKey { int x, y, z; bool operator<(const PosKey &o) const {
        if (x != o.x) return x < o.x; if (y != o.y) return y < o.y; return z < o.z;
    }};
    std::map<PosKey, Vec3> accum;

    auto snap = [](float v) -> int { return (int)std::round(v * 10000.0f); };
    auto key_of = [&snap](const Vec3 &p) -> PosKey {
        return { snap(p.x), snap(p.y), snap(p.z) };
    };

    for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3)
    {
        const Vec3 &a = mesh.positions[i];
        const Vec3 &b = mesh.positions[i + 1];
        const Vec3 &c = mesh.positions[i + 2];
        Vec3 e1 = Vec3Sub(b, a);
        Vec3 e2 = Vec3Sub(c, a);
        Vec3 fn = Vec3Normalize(Vec3Cross(e1, e2));

        for (int v = 0; v < 3; ++v)
        {
            PosKey k = key_of(mesh.positions[i + v]);
            Vec3 &acc = accum[k];
            acc = Vec3Add(acc, fn);
        }
    }

    // Normalize accumulated normals and write back.
    for (size_t i = 0; i < mesh.positions.size(); ++i)
    {
        PosKey k = key_of(mesh.positions[i]);
        auto it = accum.find(k);
        if (it != accum.end())
            mesh.normals[i] = Vec3Normalize(it->second);
        else
            mesh.normals[i] = { 0.0f, 1.0f, 0.0f };
    }
}

MeshLibrary::MeshLibrary()
{
    Mesh cube = BuildCubeMesh();
    ComputeVertexNormals(cube);
    m_meshes.emplace(kBuiltinCubePath, std::move(cube));

    // Block-out primitives: 4m-wide wall (3m tall, 0.3m thick), a 4x4m floor
    // slab (0.2m thick), and a 4m-long, 2m-high, 3m-wide ramp. Generous but
    // plain round numbers -- meant to be scaled per-instance via the
    // transform, not precisely sized up front.
    Mesh wall = BuildBoxMesh("Wall Primitive", 2.0f, 0.0f, 3.0f, 0.15f);
    ComputeVertexNormals(wall);
    m_meshes.emplace(kBuiltinWallPath, std::move(wall));

    Mesh floor = BuildBoxMesh("Floor Primitive", 2.0f, 0.0f, 0.2f, 2.0f);
    ComputeVertexNormals(floor);
    m_meshes.emplace(kBuiltinFloorPath, std::move(floor));

    Mesh ramp = BuildRampMesh(2.0f, 2.0f, 1.5f);
    ComputeVertexNormals(ramp);
    m_meshes.emplace(kBuiltinRampPath, std::move(ramp));
}

const Mesh* MeshLibrary::Load(const std::string &path, std::string *error)
{
    auto it = m_meshes.find(path);
    if (it != m_meshes.end())
        return &it->second;

    std::string resolved = path;
    if (!FileExists(resolved))
    {
        const std::string alt = "assets/meshes/" + path;
        if (FileExists(alt))
            resolved = alt;
    }

    Mesh mesh;
    if (!LoadObj(resolved, mesh, error))
        return nullptr;
    mesh.name = path;
    ComputeVertexNormals(mesh);

    auto res = m_meshes.emplace(path, std::move(mesh));
    return &res.first->second;
}

const Mesh* MeshLibrary::Get(const std::string &path) const
{
    auto it = m_meshes.find(path);
    return (it == m_meshes.end()) ? nullptr : &it->second;
}

const Mesh* MeshLibrary::GetOrLoad(const std::string &path, std::string *error)
{
    if (const Mesh *m = Get(path))
        return m;
    return Load(path, error);
}

const Mesh* MeshLibrary::GetBuiltinCube() const
{
    return Get(kBuiltinCubePath);
}

size_t MeshLibrary::ResidentBytes() const
{
    size_t bytes = 0;
    for (const auto &kv : m_meshes)
    {
        const Mesh &m = kv.second;
        bytes += sizeof(kv.second) + m.name.capacity();
        bytes += m.positions.capacity() * sizeof(Vec3);
        bytes += m.normals.capacity() * sizeof(Vec3);
        bytes += m.edge_lines.capacity() * sizeof(Vec3);
        bytes += m.uvs.capacity() * sizeof(Vec2);
    }
    return bytes;
}
