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
// relative index). Returns true and 0-based index into `verts` on success.
bool ParseFaceIndex(const std::string &tok, const std::vector<Vec3> &verts, unsigned &out)
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
    out = (unsigned)(idx - 1);
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
    std::vector<std::vector<unsigned>> faces;
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
        else if (kw == "f")
        {
            std::vector<unsigned> face;
            std::string tok;
            while (ss >> tok)
            {
                unsigned idx;
                if (ParseFaceIndex(tok, verts, idx))
                    face.push_back(idx);
            }
            if (face.size() >= 3)
                faces.push_back(std::move(face));
        }
        // "vt", "vn", "vp", "g", "o", "s", "mtllib", "usemtl" are ignored.
    }

    if (verts.empty() || faces.empty())
    {
        if (error) *error = "mesh file '" + path + "' contains no renderable geometry";
        return false;
    }

    mesh.positions.clear();
    mesh.edge_lines.clear();
    mesh.positions.reserve(faces.size() * 3);

    std::set<EdgeKey> edge_set;
    for (const auto &face : faces)
    {
        // Unique edge set for the wireframe pass (dedupes shared edges).
        for (size_t i = 0; i < face.size(); ++i)
        {
            unsigned a = face[i];
            unsigned b = face[(i + 1) % face.size()];
            if (a == b) continue;
            edge_set.insert({ std::min(a, b), std::max(a, b) });
        }
        // Fan triangulation of the (possibly convex) polygon.
        for (size_t i = 1; i + 1 < face.size(); ++i)
        {
            mesh.positions.push_back(verts[face[0]]);
            mesh.positions.push_back(verts[face[i]]);
            mesh.positions.push_back(verts[face[i + 1]]);
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

MeshLibrary::MeshLibrary()
{
    m_meshes.emplace("__builtin_cube__", BuildCubeMesh());
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
    return Get("__builtin_cube__");
}
