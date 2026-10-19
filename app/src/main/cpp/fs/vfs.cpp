#include "vfs.h"
#include "../core/common.h"
#include "../format/lzma_valve.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <mutex>
#include <set>

namespace cs {

// ============================================================ RAFile
class OsFile : public RAFile {
public:
    OsFile(int fd, uint64_t sz) : fd_(fd), size_(sz) {}
    ~OsFile() override { if (fd_ >= 0) close(fd_); }
    uint64_t size() const override { return size_; }
    bool read(uint64_t off, void* dst, size_t n) override {
        if (off + n > size_) return false;
        uint8_t* p = (uint8_t*)dst;
        while (n > 0) {
            ssize_t r = pread(fd_, p, n, (off_t)off);
            if (r <= 0) return false;
            p += r; n -= (size_t)r; off += (uint64_t)r;
        }
        return true;
    }
private:
    int fd_;
    uint64_t size_;
};

// File lepas dibuka saat dibaca (ribuan file — jangan simpan fd terbuka)
class LazyOsFile : public RAFile {
public:
    LazyOsFile(std::string p, uint64_t sz) : path_(std::move(p)), size_(sz) {}
    uint64_t size() const override { return size_; }
    bool read(uint64_t off, void* dst, size_t n) override {
        int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        OsFile f(fd, size_);
        return f.read(off, dst, n);
    }
private:
    std::string path_;
    uint64_t size_;
};

class MemFile : public RAFile {
public:
    explicit MemFile(std::vector<uint8_t>&& d) : data_(std::move(d)) {}
    uint64_t size() const override { return data_.size(); }
    bool read(uint64_t off, void* dst, size_t n) override {
        if (off + n > data_.size()) return false;
        memcpy(dst, data_.data() + off, n);
        return true;
    }
private:
    std::vector<uint8_t> data_;
};

class SubFile : public RAFile {
public:
    SubFile(std::shared_ptr<RAFile> p, uint64_t o, uint64_t s) : parent_(std::move(p)), off_(o), size_(s) {}
    uint64_t size() const override { return size_; }
    bool read(uint64_t off, void* dst, size_t n) override {
        if (off + n > size_) return false;
        return parent_->read(off_ + off, dst, n);
    }
private:
    std::shared_ptr<RAFile> parent_;
    uint64_t off_, size_;
};

std::shared_ptr<RAFile> openOsFile(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return nullptr;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return nullptr; }
    return std::make_shared<OsFile>(fd, (uint64_t)st.st_size);
}
std::shared_ptr<RAFile> makeMemFile(std::vector<uint8_t>&& data) { return std::make_shared<MemFile>(std::move(data)); }
std::shared_ptr<RAFile> makeSubFile(std::shared_ptr<RAFile> parent, uint64_t off, uint64_t size) {
    return std::make_shared<SubFile>(std::move(parent), off, size);
}
bool readWhole(RAFile& f, std::vector<uint8_t>& out) {
    out.resize((size_t)f.size());
    return out.empty() || f.read(0, out.data(), out.size());
}

// ============================================================ kunci path
static const char* kRoots[] = {"materials", "maps", "models", "sound", "scripts", "resource",
                               "particles", "cfg", "media", "shaders", "expressions", "scenes", nullptr};

std::string strip360(const std::string& key) {
    std::string k = key;
    size_t p;
    while ((p = k.find(".360.")) != std::string::npos) k.erase(p, 4);
    if (endsWith(k, ".360")) k.erase(k.size() - 4);
    return k;
}

std::string gameKeyFromRaw(const std::string& rawPath) {
    std::string p = normPath(rawPath);
    // cari komponen pertama yang merupakan folder root game
    size_t start = 0;
    while (start < p.size()) {
        size_t slash = p.find('/', start);
        if (slash == std::string::npos) break;
        std::string comp = p.substr(start, slash - start);
        for (int i = 0; kRoots[i]; ++i)
            if (comp == kRoots[i]) return p.substr(start);
        start = slash + 1;
    }
    return p;
}

void Vfs::registerKey(std::unordered_map<std::string, VfsEntry>& map, const std::string& rawPath, const VfsEntry& e) {
    std::string key = gameKeyFromRaw(rawPath);
    map.emplace(key, e);
    std::string s = strip360(key);
    if (s != key) map.emplace(s, e);
}

void Vfs::addRaw(const std::string& relPath, std::shared_ptr<RAFile> f) {
    std::string k = normPath(relPath);
    if (raw_.emplace(k, std::move(f)).second) rawOrder_.push_back(k);
}

// ============================================================ ZIP
size_t Vfs::mountZip(std::shared_ptr<RAFile> f, std::unordered_map<std::string, VfsEntry>& target, const std::string& label) {
    uint64_t fsz = f->size();
    if (fsz < 22) return 0;
    size_t tailLen = (size_t)std::min<uint64_t>(fsz, 65557);
    std::vector<uint8_t> tail(tailLen);
    if (!f->read(fsz - tailLen, tail.data(), tailLen)) return 0;
    int eocd = -1;
    for (int i = (int)tailLen - 22; i >= 0; --i)
        if (rd32le(&tail[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { LOGW("ZIP tanpa EOCD: %s", label.c_str()); return 0; }
    uint32_t count = rd16le(&tail[eocd + 10]);
    uint32_t cdSize = rd32le(&tail[eocd + 12]);
    uint32_t cdOff = rd32le(&tail[eocd + 16]);
    if ((uint64_t)cdOff + cdSize > fsz) { LOGW("ZIP CD rusak: %s", label.c_str()); return 0; }
    std::vector<uint8_t> cd(cdSize);
    if (!f->read(cdOff, cd.data(), cdSize)) return 0;
    size_t n = 0, p = 0;
    for (uint32_t i = 0; i < count && p + 46 <= cd.size(); ++i) {
        if (rd32le(&cd[p]) != 0x02014b50) break;
        uint16_t method = rd16le(&cd[p + 10]);
        uint32_t csize = rd32le(&cd[p + 20]), usize = rd32le(&cd[p + 24]);
        uint16_t nl = rd16le(&cd[p + 28]), el = rd16le(&cd[p + 30]), cl = rd16le(&cd[p + 32]);
        uint32_t lho = rd32le(&cd[p + 42]);
        if (p + 46 + nl > cd.size()) break;
        std::string name((const char*)&cd[p + 46], nl);
        p += 46 + nl + el + cl;
        if (name.empty() || name.back() == '/') continue;
        VfsEntry e;
        e.src = f; e.offset = lho; e.csize = csize; e.usize = usize; e.method = method; e.zipLocal = true;
        registerKey(target, name, e);
        ++n;
    }
    return n;
}

// ============================================================ VPK
size_t Vfs::mountVpk(const std::string& dirRawPath, std::shared_ptr<RAFile> f) {
    uint8_t h[28];
    if (f->size() < 12 || !f->read(0, h, std::min<uint64_t>(28, f->size()))) return 0;
    bool be = false;
    if (rd32le(h) == 0x55aa1234) be = false;
    else if (rd32be(h) == 0x55aa1234) be = true;
    else return 0;
    auto r32 = [&](const uint8_t* p) { return be ? rd32be(p) : rd32le(p); };
    auto r16 = [&](const uint8_t* p) { return be ? rd16be(p) : rd16le(p); };
    uint32_t ver = r32(h + 4), treeSize = r32(h + 8);
    uint32_t headerSize = ver == 1 ? 12 : ver == 2 ? 28 : 0;
    if (!headerSize || headerSize + (uint64_t)treeSize > f->size()) { LOGW("VPK versi %u tidak didukung", ver); return 0; }
    std::vector<uint8_t> tree(treeSize);
    if (!f->read(headerSize, tree.data(), treeSize)) return 0;

    std::string base = dirRawPath.substr(0, dirRawPath.size() - 8); // buang "_dir.vpk"
    std::unordered_map<int, std::shared_ptr<RAFile>> archives;
    auto archiveFor = [&](int idx) -> std::shared_ptr<RAFile> {
        auto it = archives.find(idx);
        if (it != archives.end()) return it->second;
        char suf[16];
        snprintf(suf, sizeof(suf), "_%03d.vpk", idx);
        std::shared_ptr<RAFile> af;
        auto rit = raw_.find(base + suf);
        if (rit != raw_.end()) af = rit->second;
        archives[idx] = af;
        return af;
    };

    size_t p = 0, n = 0;
    auto readStr = [&](std::string& s) -> bool {
        size_t e = p;
        while (e < tree.size() && tree[e]) ++e;
        if (e >= tree.size()) return false;
        s.assign((const char*)&tree[p], e - p);
        p = e + 1;
        return true;
    };
    std::string ext, dir, file;
    while (readStr(ext) && !ext.empty()) {
        while (readStr(dir) && !dir.empty()) {
            while (readStr(file) && !file.empty()) {
                if (p + 18 > tree.size()) return n;
                uint16_t preload = r16(&tree[p + 4]);
                uint16_t arch = r16(&tree[p + 6]);
                uint32_t off = r32(&tree[p + 8]), len = r32(&tree[p + 12]);
                p += 18;
                VfsEntry e;
                if (preload) {
                    if (p + preload > tree.size()) return n;
                    e.preload = std::make_shared<std::vector<uint8_t>>(tree.begin() + p, tree.begin() + p + preload);
                    p += preload;
                }
                if (arch == 0x7fff) { e.src = f; e.offset = headerSize + treeSize + off; }
                else { e.src = archiveFor(arch); e.offset = off; }
                e.csize = e.usize = len;
                if (!e.src && len) continue;
                std::string full = (dir == " " ? "" : dir + "/") + file + "." + ext;
                registerKey(index_, full, e);
                ++n;
            }
        }
    }
    return n;
}

// ============================================================ STFS (LIVE/PIRS/CON)
class StfsPackage {
public:
    std::shared_ptr<RAFile> f;
    uint32_t headerSize = 0, firstHashTable = 0;
    int sex = 0;
    uint32_t blockStep0 = 0xAB;
    uint32_t allocated = 0;

    uint64_t blockToAddr(uint32_t b) const {
        uint32_t r = (((b + 0xAA) / 0xAA) << sex) + b;
        if (b >= 0xAA) {
            r += ((b + 0x70E4) / 0x70E4) << sex;
            if (b >= 0x70E4) r += 1u << sex;
        }
        return (uint64_t)firstHashTable + ((uint64_t)r << 12);
    }
    uint64_t level0HashAddr(uint32_t b) const {
        uint32_t num = 0;
        if (b >= 0xAA) {
            num = (b / 0xAA) * blockStep0;
            num += ((b / 0x70E4) + 1) << sex;
            if (b / 0x70E4 != 0) num += 1u << sex;
        }
        return (uint64_t)firstHashTable + ((uint64_t)num << 12) + (b % 0xAA) * 0x18;
    }
    uint32_t nextBlock(uint32_t b) const {
        uint8_t e[0x18];
        uint64_t a = level0HashAddr(b);
        if (!f->read(a, e, 0x18)) return 0xFFFFFF;
        uint32_t nb = rd24be(e + 0x15);
        if (sex == 1 && (nb == 0 || (allocated && nb > allocated && nb != 0xFFFFFF))) {
            if (f->read(a + 0x1000, e, 0x18)) nb = rd24be(e + 0x15);
        }
        return nb;
    }
};

class StfsFile : public RAFile {
public:
    StfsFile(std::shared_ptr<StfsPackage> pkg, uint32_t start, uint32_t size, bool consecutive, uint32_t nblocks)
        : pkg_(std::move(pkg)), start_(start), size_(size), consecutive_(consecutive), nblocks_(nblocks) {}
    uint64_t size() const override { return size_; }
    bool read(uint64_t off, void* dst, size_t n) override {
        if (off + n > size_) return false;
        if (!consecutive_) {
            std::lock_guard<std::mutex> lk(m_);
            if (blocks_.empty()) buildChain();
        }
        uint8_t* p = (uint8_t*)dst;
        while (n > 0) {
            uint32_t bi = (uint32_t)(off >> 12), bo = (uint32_t)(off & 0xFFF);
            uint32_t blk;
            if (consecutive_) blk = start_ + bi;
            else { if (bi >= blocks_.size()) return false; blk = blocks_[bi]; }
            size_t take = std::min<size_t>(n, 0x1000 - bo);
            if (!pkg_->f->read(pkg_->blockToAddr(blk) + bo, p, take)) return false;
            p += take; n -= take; off += take;
        }
        return true;
    }
private:
    void buildChain() {
        uint32_t b = start_;
        for (uint32_t i = 0; i < nblocks_ && b != 0xFFFFFF; ++i) {
            blocks_.push_back(b);
            b = pkg_->nextBlock(b);
        }
    }
    std::shared_ptr<StfsPackage> pkg_;
    uint32_t start_, size_;
    bool consecutive_;
    uint32_t nblocks_;
    std::vector<uint32_t> blocks_;
    std::mutex m_;
};

bool Vfs::mountStfs(const std::string& path, std::shared_ptr<RAFile> f, ContainerInfo& info) {
    std::vector<uint8_t> h(0x971);
    if (f->size() < h.size() || !f->read(0, h.data(), h.size())) return false;
    auto pkg = std::make_shared<StfsPackage>();
    pkg->f = f;
    pkg->headerSize = rd32be(&h[0x340]);
    uint32_t contentType = rd32be(&h[0x344]);
    uint32_t descType = rd32be(&h[0x3A9]);
    char ctype[16];
    snprintf(ctype, sizeof(ctype), "0x%08X", contentType);
    info.detail = std::string((const char*)h.data(), 4) + " tipe " + ctype;
    if (descType == 1 || contentType == 0x7000) {
        info.kind = ContainerKind::Unsupported;
        info.ok = false;
        info.detail += " (Games-on-Demand/SVOD belum didukung — ubah ke ISO atau ekstrak isinya)";
        return false;
    }
    const uint8_t* vd = &h[0x379];
    uint8_t blockSep = vd[2];
    uint32_t ftCount = rd16le(vd + 3);
    uint32_t ftBlock = rd24le(vd + 5);
    pkg->allocated = rd32be(vd + 0x1C);
    pkg->sex = (~blockSep) & 1;
    pkg->blockStep0 = pkg->sex ? 0xAC : 0xAB;
    pkg->firstHashTable = (pkg->headerSize + 0xFFF) & 0xFFFFF000u;
    if (ftCount == 0 || ftCount > 0x4000) { ftCount = rd16be(vd + 3); }
    LOGI("STFS %s: header=0x%X sex=%d ftBlocks=%u ftStart=%u", path.c_str(), pkg->headerSize, pkg->sex, ftCount, ftBlock);

    // baca tabel file
    std::vector<uint8_t> table;
    uint32_t b = ftBlock;
    for (uint32_t i = 0; i < ftCount; ++i) {
        size_t o = table.size();
        table.resize(o + 0x1000);
        if (!f->read(pkg->blockToAddr(b), &table[o], 0x1000)) { table.resize(o); break; }
        uint32_t nb = pkg->nextBlock(b);
        b = (nb == 0xFFFFFF || nb == 0) ? b + 1 : nb;
    }
    struct Ent { std::string name; bool dir = false, valid = false; uint32_t start = 0, size = 0, blocks = 0; bool consec = false; int parent = -1; };
    // pathIndicator merujuk ke posisi slot di tabel, jadi slot kosong tetap disimpan
    std::vector<Ent> ents;
    for (size_t o = 0; o + 0x40 <= table.size(); o += 0x40) {
        const uint8_t* e = &table[o];
        ents.emplace_back();
        if (e[0] == 0) continue;
        uint8_t flags = e[0x28];
        int nl = flags & 0x3F;
        if (nl == 0 || nl > 0x28) continue;
        Ent& en = ents.back();
        en.valid = true;
        en.name.assign((const char*)e, nl);
        en.dir = (flags & 0x80) != 0;
        en.consec = (flags & 0x40) != 0;
        en.blocks = rd24le(e + 0x29);
        en.start = rd24le(e + 0x2F);
        uint16_t pi = rd16be(e + 0x32);
        en.parent = pi == 0xFFFF ? -1 : (int)pi;
        en.size = rd32be(e + 0x34);
    }
    // ents diindeks sesuai urutan di tabel; pathIndicator merujuk indeks entri
    std::vector<std::string> fullPath(ents.size());
    std::function<std::string(int, int)> pathOf = [&](int i, int depth) -> std::string {
        if (i < 0 || i >= (int)ents.size() || !ents[i].valid || depth > 64) return "";
        if (!fullPath[i].empty()) return fullPath[i];
        std::string pp = pathOf(ents[i].parent, depth + 1);
        fullPath[i] = pp.empty() ? ents[i].name : pp + "/" + ents[i].name;
        return fullPath[i];
    };
    size_t files = 0;
    for (int i = 0; i < (int)ents.size(); ++i) {
        if (!ents[i].valid || ents[i].dir) continue;
        std::string fp = pathOf(i, 0);
        uint32_t nblocks = (ents[i].size + 0xFFF) >> 12;
        addRaw(fp, std::make_shared<StfsFile>(pkg, ents[i].start, ents[i].size, ents[i].consec, nblocks));
        ++files;
    }
    info.files = files;
    return files > 0;
}

// ============================================================ XDVDFS (ISO Xbox 360)
class IsoReader {
public:
    std::shared_ptr<RAFile> f;
    uint64_t base = 0;
    Vfs* vfs = nullptr;
    size_t files = 0;
    void walk(uint32_t sector, uint32_t size, const std::string& prefix, int depth) {
        if (size == 0 || depth > 32 || size > 64 * 1024 * 1024) return;
        std::vector<uint8_t> d(size);
        if (!f->read(base + (uint64_t)sector * 2048, d.data(), size)) return;
        std::set<uint32_t> seen;
        std::vector<uint32_t> stack{0};
        while (!stack.empty()) {
            uint32_t o = stack.back();
            stack.pop_back();
            if (o + 14 > size || !seen.insert(o).second) continue;
            uint16_t l = rd16le(&d[o]), r = rd16le(&d[o + 2]);
            if (l == 0xFFFF && r == 0xFFFF) continue;
            uint32_t sec = rd32le(&d[o + 4]), sz = rd32le(&d[o + 8]);
            uint8_t attr = d[o + 12], nl = d[o + 13];
            if (o + 14 + nl > size) continue;
            std::string name((const char*)&d[o + 14], nl);
            if (l) stack.push_back((uint32_t)l * 4);
            if (r) stack.push_back((uint32_t)r * 4);
            std::string full = prefix.empty() ? name : prefix + "/" + name;
            if (attr & 0x10) walk(sec, sz, full, depth + 1);
            else {
                vfs->addRaw(full, makeSubFile(f, base + (uint64_t)sec * 2048, sz));
                ++files;
            }
        }
    }
};

bool Vfs::mountIso(const std::string& path, std::shared_ptr<RAFile> f, ContainerInfo& info) {
    static const uint64_t parts[] = {0, 0xFD90000ull, 0x2080000ull, 0x18300000ull};
    static const char* names[] = {"ISO polos", "XGD2", "XGD3", "XGD1"};
    for (int i = 0; i < 4; ++i) {
        uint8_t vd[0x1C];
        uint64_t at = parts[i] + 0x10000;
        if (at + sizeof(vd) > f->size() || !f->read(at, vd, sizeof(vd))) continue;
        if (memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20) != 0) continue;
        IsoReader r;
        r.f = f; r.base = parts[i]; r.vfs = this;
        r.walk(rd32le(vd + 0x14), rd32le(vd + 0x18), "", 0);
        info.detail = std::string("XDVDFS ") + names[i];
        info.files = r.files;
        LOGI("ISO %s: %s, %zu file", path.c_str(), names[i], r.files);
        return r.files > 0;
    }
    return false;
}

// ============================================================ pemindaian
static bool isDir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }

static ContainerKind sniff(const std::string& path, RAFile& f) {
    uint8_t m[4] = {0};
    if (f.size() >= 4) f.read(0, m, 4);
    if (!memcmp(m, "LIVE", 4) || !memcmp(m, "PIRS", 4) || !memcmp(m, "CON ", 4)) return ContainerKind::Stfs;
    std::string lp = toLower(path);
    if (endsWith(lp, ".iso") || endsWith(lp, ".xiso")) return ContainerKind::Iso;
    // ISO tanpa ekstensi: cek magic XDVDFS
    for (uint64_t part : {0ull, 0xFD90000ull, 0x2080000ull, 0x18300000ull}) {
        uint8_t vd[20];
        if (part + 0x10000 + 20 <= f.size() && f.read(part + 0x10000, vd, 20) && !memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20))
            return ContainerKind::Iso;
    }
    return ContainerKind::Folder;
}

static bool skipTopDir(const std::string& name) {
    std::string n = toLower(name);
    return n == "saves" || n == "logs" || n == "cache" || n == "drivers" || n == "screenshots";
}

void Vfs::walkDir(const std::string& root, const std::string& rel, ContainerInfo& folderInfo, const Progress& progress, int depth) {
    if (depth > 24) return;
    std::string full = rel.empty() ? root : root + "/" + rel;
    DIR* d = opendir(full.c_str());
    if (!d) return;
    std::vector<std::pair<std::string, bool>> items;
    while (dirent* de = readdir(d)) {
        std::string n = de->d_name;
        if (n == "." || n == "..") continue;
        bool dir = de->d_type == DT_DIR || (de->d_type == DT_UNKNOWN && isDir(full + "/" + n));
        items.emplace_back(n, dir);
    }
    closedir(d);
    std::sort(items.begin(), items.end());
    for (auto& it : items) {
        std::string r = rel.empty() ? it.first : rel + "/" + it.first;
        std::string fp = root + "/" + r;
        if (it.second) {
            if (depth == 0 && skipTopDir(it.first)) continue;
            walkDir(root, r, folderInfo, progress, depth + 1);
            continue;
        }
        struct stat st;
        if (stat(fp.c_str(), &st) != 0) continue;
        std::string ln = toLower(it.first);
        bool noExt = ln.find('.') == std::string::npos;
        if (noExt || endsWith(ln, ".iso") || endsWith(ln, ".xiso") || endsWith(ln, ".stfs") || endsWith(ln, ".bin")) {
            auto f = openOsFile(fp);
            if (f) {
                ContainerKind k = sniff(fp, *f);
                if (k == ContainerKind::Stfs || k == ContainerKind::Iso) {
                    ContainerInfo ci;
                    ci.path = fp;
                    ci.kind = k;
                    if (progress) progress("Memasang " + it.first);
                    ci.ok = (k == ContainerKind::Stfs) ? mountStfs(fp, f, ci) : mountIso(fp, f, ci);
                    if (k == ContainerKind::Stfs && ci.kind == ContainerKind::Unsupported) ci.ok = false;
                    containers_.push_back(ci);
                    continue;
                }
            }
        }
        addRaw(r, std::make_shared<LazyOsFile>(fp, (uint64_t)st.st_size));
        folderInfo.files++;
    }
}

bool Vfs::mountPath(const std::string& path, const Progress& progress) {
    if (isDir(path)) {
        ContainerInfo fi;
        fi.path = path;
        fi.kind = ContainerKind::Folder;
        walkDir(path, "", fi, progress, 0);
        if (fi.files) { fi.detail = "folder file lepas"; containers_.push_back(fi); }
        return true;
    }
    auto f = openOsFile(path);
    if (!f) return false;
    ContainerInfo ci;
    ci.path = path;
    ci.kind = sniff(path, *f);
    bool ok = false;
    if (ci.kind == ContainerKind::Stfs) ok = mountStfs(path, f, ci);
    else if (ci.kind == ContainerKind::Iso) ok = mountIso(path, f, ci);
    else {
        std::string name = path.substr(path.find_last_of('/') + 1);
        addRaw(name, f);
        ok = true;
    }
    ci.ok = ok;
    containers_.push_back(ci);
    return ok;
}

void Vfs::finalizeArchives(const Progress& progress) {
    // 1) file mentah (prioritas tertinggi, seperti file lepas di Source)
    for (auto& k : rawOrder_) {
        VfsEntry e;
        e.src = raw_[k];
        e.csize = e.usize = (uint32_t)e.src->size();
        registerKey(index_, k, e);
    }
    // 2) arsip ZIP / VPK di dalam kontainer
    for (auto& k : rawOrder_) {
        if (endsWith(k, ".zip")) {
            if (progress) progress("Membaca arsip " + k);
            ContainerInfo ci;
            ci.path = k;
            ci.kind = ContainerKind::Zip;
            ci.files = mountZip(raw_[k], index_, k);
            ci.ok = ci.files > 0;
            ci.detail = "arsip ZIP";
            containers_.push_back(ci);
        } else if (endsWith(k, "_dir.vpk")) {
            if (progress) progress("Membaca arsip " + k);
            ContainerInfo ci;
            ci.path = k;
            ci.kind = ContainerKind::Vpk;
            ci.files = mountVpk(k, raw_[k]);
            ci.ok = ci.files > 0;
            ci.detail = "arsip VPK";
            containers_.push_back(ci);
        }
    }
    LOGI("VFS: %zu file mentah, %zu entri total", rawOrder_.size(), index_.size());
}

void Vfs::mountGameDir(const std::string& dir, const Progress& progress) {
    mountPath(dir, progress);
    finalizeArchives(progress);
}

// ============================================================ akses
const VfsEntry* Vfs::find(const std::string& path) const {
    std::string k = normPath(path);
    auto it = overlay_.find(k);
    if (it != overlay_.end()) return &it->second;
    auto jt = index_.find(k);
    if (jt != index_.end()) return &jt->second;
    std::string s = strip360(k);
    if (s != k) {
        it = overlay_.find(s);
        if (it != overlay_.end()) return &it->second;
        jt = index_.find(s);
        if (jt != index_.end()) return &jt->second;
    }
    return nullptr;
}

bool Vfs::exists(const std::string& path) const { return find(path) != nullptr; }

static bool inflateRaw(const uint8_t* src, size_t n, size_t outSize, std::vector<uint8_t>& out) {
    out.resize(outSize);
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)n;
    zs.next_out = out.data();
    zs.avail_out = (uInt)outSize;
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return r == Z_STREAM_END || zs.total_out == outSize;
}

bool Vfs::readEntry(const VfsEntry& e, std::vector<uint8_t>& out) const {
    if (!e.src && !e.preload) return false;
    uint64_t off = e.offset;
    if (e.zipLocal) {
        uint8_t lh[30];
        if (!e.src->read(off, lh, 30) || rd32le(lh) != 0x04034b50) return false;
        off += 30 + rd16le(lh + 26) + rd16le(lh + 28);
    }
    std::vector<uint8_t> raw;
    size_t pre = e.preload ? e.preload->size() : 0;
    raw.resize(pre + e.csize);
    if (pre) memcpy(raw.data(), e.preload->data(), pre);
    if (e.csize && !e.src->read(off, raw.data() + pre, e.csize)) return false;

    if (e.method == 8) {
        if (!inflateRaw(raw.data(), raw.size(), e.usize, out)) return false;
    } else if (e.method == 14) {
        if (raw.size() < 9) return false;
        uint16_t propSize = rd16le(&raw[2]);
        if (4 + propSize > raw.size() || propSize < 5) return false;
        if (!decompressRawLzma(&raw[4], &raw[4 + propSize], raw.size() - 4 - propSize, e.usize, out)) return false;
    } else {
        out.swap(raw);
    }
    // File di ZIP Xbox 360 sering dikompres LZMA dengan header Valve
    if (isValveLzma(out.data(), out.size())) {
        std::vector<uint8_t> dec;
        if (decompressValveLzma(out.data(), out.size(), dec)) out.swap(dec);
    }
    return true;
}

bool Vfs::readFile(const std::string& path, std::vector<uint8_t>& out) const {
    const VfsEntry* e = find(path);
    return e && readEntry(*e, out);
}

std::vector<std::string> Vfs::listPrefix(const std::string& prefix) const {
    std::set<std::string> s;
    std::string p = normPath(prefix);
    for (auto& kv : index_)
        if (startsWith(kv.first, p)) s.insert(kv.first);
    return {s.begin(), s.end()};
}

std::vector<std::string> Vfs::listMaps() const {
    std::set<std::string> s;
    for (auto& kv : index_) {
        const std::string& k = kv.first;
        if (startsWith(k, "maps/") && endsWith(k, ".bsp") && k.find('/', 5) == std::string::npos) {
            std::string n = strip360(k.substr(5));
            n = n.substr(0, n.size() - 4);
            s.insert(n);
        }
    }
    return {s.begin(), s.end()};
}

void Vfs::setOverlayZip(std::shared_ptr<RAFile> zip) {
    overlay_.clear();
    size_t n = mountZip(std::move(zip), overlay_, "pakfile");
    LOGI("Pakfile peta: %zu file", n);
}

} // namespace cs
