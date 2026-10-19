// Parser material Valve (.vmt, KeyValues teks) dengan dukungan shader "patch".
#pragma once
#include <map>
#include <string>
#include <vector>

namespace cs {

class Vfs;

struct KvNode {
    std::string key, value;
    std::vector<KvNode> children;
    bool isBlock = false;
    const KvNode* child(const std::string& k) const;
};

bool parseKeyValues(const std::string& text, KvNode& root);

struct Material {
    std::string name;
    std::string shader;                       // huruf kecil
    std::map<std::string, std::string> params; // kunci huruf kecil, mis. "$basetexture"
    bool found = false;

    std::string get(const std::string& k, const std::string& def = "") const;
    bool flag(const std::string& k) const;
    float getFloat(const std::string& k, float def) const;
    bool getColor(const std::string& k, float rgb[3]) const;
};

// Muat "materials/<name>.vmt" melalui VFS; mengikuti "patch"/"include" (maks 4 tingkat)
Material loadMaterial(const Vfs& vfs, const std::string& name);

} // namespace cs
