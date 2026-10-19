// Virtual filesystem: memasang file game asli Xbox 360 milik pengguna.
//  - Folder hasil ekstrak (file lepas)
//  - Paket STFS (LIVE / PIRS / CON) — format paket XBLA CS:GO Xbox 360
//  - ISO XDVDFS (XGD1/XGD2/XGD3 atau ISO polos)
//  - Arsip ZIP Valve (*.360.zip, termasuk entri LZMA) dan VPK (v1/v2, LE/BE)
// Semua path dinormalisasi; sufiks ".360." dihapus sehingga "maps/de_dust2.360.bsp"
// juga bisa diakses sebagai "maps/de_dust2.bsp".
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cs {

class RAFile {
public:
    virtual ~RAFile() = default;
    virtual uint64_t size() const = 0;
    virtual bool read(uint64_t off, void* dst, size_t n) = 0;
};

std::shared_ptr<RAFile> openOsFile(const std::string& path);
std::shared_ptr<RAFile> makeMemFile(std::vector<uint8_t>&& data);
std::shared_ptr<RAFile> makeSubFile(std::shared_ptr<RAFile> parent, uint64_t off, uint64_t size);
bool readWhole(RAFile& f, std::vector<uint8_t>& out);

enum class ContainerKind { Folder, Stfs, Iso, Zip, Vpk, Unsupported };

struct ContainerInfo {
    std::string path;
    ContainerKind kind = ContainerKind::Folder;
    std::string detail;
    size_t files = 0;
    bool ok = true;
};

struct VfsEntry {
    std::shared_ptr<RAFile> src;
    uint64_t offset = 0;      // offset data (atau header lokal ZIP jika zipLocal)
    uint32_t csize = 0;
    uint32_t usize = 0;
    uint16_t method = 0;      // 0 simpan, 8 deflate, 14 lzma
    bool zipLocal = false;    // offset menunjuk ke local header ZIP
    std::shared_ptr<std::vector<uint8_t>> preload; // preload VPK
};

class Vfs {
public:
    using Progress = std::function<void(const std::string&)>;

    // Pindai folder game: pasang semua kontainer yang dikenali.
    void mountGameDir(const std::string& dir, const Progress& progress = nullptr);
    // Pasang satu file kontainer (STFS/ISO/ZIP/VPK) atau folder.
    bool mountPath(const std::string& path, const Progress& progress = nullptr);

    bool exists(const std::string& path) const;
    bool readFile(const std::string& path, std::vector<uint8_t>& out) const;
    std::vector<std::string> listMaps() const;
    std::vector<std::string> listPrefix(const std::string& prefix) const;
    const std::vector<ContainerInfo>& containers() const { return containers_; }
    size_t fileCount() const { return index_.size(); }

    // Lapisan atas (pakfile peta) — menimpa index utama
    void setOverlayZip(std::shared_ptr<RAFile> zip);
    void clearOverlay() { overlay_.clear(); }

    // Untuk uji/host: daftarkan file mentah
    void addRaw(const std::string& relPath, std::shared_ptr<RAFile> f);
    void finalizeArchives(const Progress& progress = nullptr);

private:
    const VfsEntry* find(const std::string& path) const;
    bool readEntry(const VfsEntry& e, std::vector<uint8_t>& out) const;
    void registerKey(std::unordered_map<std::string, VfsEntry>& map, const std::string& rawPath, const VfsEntry& e);
    size_t mountZip(std::shared_ptr<RAFile> f, std::unordered_map<std::string, VfsEntry>& target, const std::string& label);
    size_t mountVpk(const std::string& dirRawPath, std::shared_ptr<RAFile> f);
    bool mountStfs(const std::string& path, std::shared_ptr<RAFile> f, ContainerInfo& info);
    bool mountIso(const std::string& path, std::shared_ptr<RAFile> f, ContainerInfo& info);
    void walkDir(const std::string& root, const std::string& rel, ContainerInfo& folderInfo, const Progress& progress, int depth);

    std::unordered_map<std::string, std::shared_ptr<RAFile>> raw_;
    std::vector<std::string> rawOrder_;
    std::unordered_map<std::string, VfsEntry> index_;
    std::unordered_map<std::string, VfsEntry> overlay_;
    std::vector<ContainerInfo> containers_;
};

// Kunci path game dari path mentah: buang awalan sampai folder root yang dikenal
std::string gameKeyFromRaw(const std::string& rawPath);
std::string strip360(const std::string& key);

} // namespace cs
