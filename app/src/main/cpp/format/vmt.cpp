#include "vmt.h"
#include "../core/common.h"
#include "../fs/vfs.h"

namespace cs {

const KvNode* KvNode::child(const std::string& k) const {
    for (auto& c : children) if (c.key == k) return &c;
    return nullptr;
}

namespace {
struct Tok {
    const std::string& t;
    size_t i = 0;
    explicit Tok(const std::string& s) : t(s) {}
    // 0 = habis, 1 = string, '{' / '}' = kurung
    int next(std::string& out) {
        while (i < t.size()) {
            char c = t[i];
            if (isspace((unsigned char)c) || c == 0) { ++i; continue; }
            if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') { while (i < t.size() && t[i] != '\n') ++i; continue; }
            if (c == '{' || c == '}') { ++i; return c; }
            if (c == '"') {
                size_t e = i + 1;
                while (e < t.size() && t[e] != '"') ++e;
                out = t.substr(i + 1, e - i - 1);
                i = std::min(e + 1, t.size());
                return 1;
            }
            size_t e = i;
            while (e < t.size() && !isspace((unsigned char)t[e]) && t[e] != '{' && t[e] != '}' && t[e] != '"') ++e;
            out = t.substr(i, e - i);
            i = e;
            return 1;
        }
        return 0;
    }
};

bool parseBlock(Tok& tk, KvNode& node, int depth) {
    if (depth > 32) return false;
    std::string k, v;
    while (true) {
        int r = tk.next(k);
        if (r == 0) return depth == 0;
        if (r == '}') return true;
        if (r == '{') { KvNode anon; anon.isBlock = true; if (!parseBlock(tk, anon, depth + 1)) return false; continue; }
        size_t save = tk.i;
        int r2 = tk.next(v);
        KvNode c;
        c.key = toLower(k);
        if (r2 == '{') {
            c.isBlock = true;
            if (!parseBlock(tk, c, depth + 1)) return false;
        } else if (r2 == 1) {
            c.value = v;
            // lewati kondisi seperti [$X360]
            size_t s2 = tk.i;
            std::string cond;
            if (tk.next(cond) == 1 && !cond.empty() && cond[0] == '[') {} else tk.i = s2;
        } else {
            tk.i = save;
            if (r2 == '}') { node.children.push_back(c); return true; }
            return depth == 0;
        }
        node.children.push_back(std::move(c));
    }
}
} // namespace

// KeyValues biner (kemungkinan dipakai file konsol): [tipe][nama\0][nilai]...; 8 = akhir blok
static bool parseBinaryKv(const uint8_t* d, size_t n, size_t& i, KvNode& node, int depth, bool be) {
    if (depth > 32) return false;
    while (i < n) {
        uint8_t type = d[i++];
        if (type == 8 || type == 11) return true;
        size_t e = i;
        while (e < n && d[e]) ++e;
        if (e >= n) return false;
        KvNode c;
        c.key = toLower(std::string((const char*)d + i, e - i));
        i = e + 1;
        auto rd32 = [&](uint32_t& v) -> bool {
            if (i + 4 > n) return false;
            v = be ? rd32be(d + i) : rd32le(d + i);
            i += 4;
            return true;
        };
        uint32_t u = 0;
        switch (type) {
            case 0:
                c.isBlock = true;
                if (!parseBinaryKv(d, n, i, c, depth + 1, be)) return false;
                break;
            case 1: {
                size_t z = i;
                while (z < n && d[z]) ++z;
                c.value.assign((const char*)d + i, z - i);
                i = z + 1;
                break;
            }
            case 2: case 4: case 6:
                if (!rd32(u)) return false;
                c.value = std::to_string((int32_t)u);
                break;
            case 3: {
                if (!rd32(u)) return false;
                float f;
                memcpy(&f, &u, 4);
                c.value = std::to_string(f);
                break;
            }
            case 7:
                if (i + 8 > n) return false;
                i += 8;
                break;
            default:
                return false;
        }
        node.children.push_back(std::move(c));
    }
    return depth == 0;
}

bool parseKeyValues(const std::string& text, KvNode& root) {
    if (!text.empty() && text[0] == 0) {
        root = KvNode();
        root.isBlock = true;
        const uint8_t* d = (const uint8_t*)text.data();
        for (bool be : {false, true}) {
            size_t i = 0;
            KvNode r;
            r.isBlock = true;
            if (parseBinaryKv(d, text.size(), i, r, 0, be) && !r.children.empty()) { root = r; return true; }
        }
        return false;
    }
    Tok tk(text);
    root = KvNode();
    root.isBlock = true;
    return parseBlock(tk, root, 0);
}

std::string Material::get(const std::string& k, const std::string& def) const {
    auto it = params.find(k);
    return it == params.end() ? def : it->second;
}
bool Material::flag(const std::string& k) const {
    auto it = params.find(k);
    return it != params.end() && atof(it->second.c_str()) != 0.0;
}
float Material::getFloat(const std::string& k, float def) const {
    auto it = params.find(k);
    return it == params.end() ? def : (float)atof(it->second.c_str());
}
bool Material::getColor(const std::string& k, float rgb[3]) const {
    auto it = params.find(k);
    if (it == params.end()) return false;
    std::string s = it->second;
    bool braces = s.find('{') != std::string::npos;
    for (auto& c : s) if (c == '[' || c == ']' || c == '{' || c == '}') c = ' ';
    float v[3] = {1, 1, 1};
    int n = sscanf(s.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
    if (n < 3) return false;
    for (int i = 0; i < 3; ++i) rgb[i] = braces ? v[i] / 255.f : v[i];
    return true;
}

static std::string materialPath(const std::string& name) {
    std::string p = normPath(name);
    if (!startsWith(p, "materials/")) p = "materials/" + p;
    if (!endsWith(p, ".vmt")) p += ".vmt";
    return p;
}

static bool loadInto(const Vfs& vfs, const std::string& path, Material& m, int depth) {
    if (depth > 4) return false;
    std::vector<uint8_t> data;
    if (!vfs.readFile(path, data)) return false;
    // lewati BOM UTF-8
    size_t start = (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) ? 3 : 0;
    KvNode root;
    parseKeyValues(std::string(data.begin() + start, data.end()), root);
    const KvNode* body = nullptr;
    for (auto& c : root.children) if (c.isBlock) { body = &c; break; }
    if (!body) return false;
    std::string shader = body->key;
    if (shader == "patch") {
        const KvNode* inc = body->child("include");
        if (inc) loadInto(vfs, normPath(inc->value), m, depth + 1);
        for (const char* sec : {"insert", "replace"}) {
            const KvNode* s = body->child(sec);
            if (s) for (auto& c : s->children) if (!c.isBlock) m.params[c.key] = c.value;
        }
        return true;
    }
    m.shader = shader;
    for (auto& c : body->children) {
        if (c.isBlock) {
            // blok fallback per-platform/dx level — ambil nilai yang belum ada
            if (c.key.find("dx9") != std::string::npos || c.key.find("360") != std::string::npos || c.key.find(">=") == 0) {
                for (auto& cc : c.children) if (!cc.isBlock && !m.params.count(cc.key)) m.params[cc.key] = cc.value;
            }
            continue;
        }
        m.params[c.key] = c.value;
    }
    return true;
}

Material loadMaterial(const Vfs& vfs, const std::string& name) {
    Material m;
    m.name = normPath(name);
    m.found = loadInto(vfs, materialPath(name), m, 0);
    return m;
}

} // namespace cs
