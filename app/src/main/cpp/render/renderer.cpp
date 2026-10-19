#include "renderer.h"
#include "../shaders/shaders_spv.h"
#include "adrenotools/driver.h"

#include <dlfcn.h>
#include <sched.h>
#include <climits>
#include <unistd.h>
#include <fstream>
#include <sys/stat.h>

namespace cs {

#define VKCHECK(x)                                                         \
    do {                                                                   \
        VkResult _r = (x);                                                 \
        if (_r != VK_SUCCESS) {                                            \
            LOGE("%s gagal: %d (%s:%d)", #x, (int)_r, __FILE__, __LINE__); \
            return false;                                                  \
        }                                                                  \
    } while (0)

static constexpr VkDeviceSize kBlockSize = 64ull << 20;
static constexpr VkDeviceSize kStagingSize = 64ull << 20;

// ================================================================ init
bool Renderer::init(const GraphicsConfig& cfg, const SessionConfig& sess, std::string& err) {
    cfg_ = cfg;
    sess_ = sess;
    applyThreadAffinity();

    // --- muat libvulkan: Turnip via adrenotools atau driver sistem
    turnipRequested = cfg.gpuDriver == 1;
    if (turnipRequested) {
        std::string drvDir = sess.driverDir;
        if (!drvDir.empty() && drvDir.back() != '/') drvDir += '/';
        std::string hookDir = sess.nativeLibDir;
        if (!hookDir.empty() && hookDir.back() != '/') hookDir += '/';
        struct stat st;
        if (sess.driverName.empty() || stat((drvDir + sess.driverName).c_str(), &st) != 0) {
            turnipError = "file driver Turnip tidak ditemukan — impor zip driver di menu Driver GPU";
        } else {
            std::string tmp = sess.filesDir + "/adrenotools_tmp/";
            mkdir(tmp.c_str(), 0700);
            libvulkan_ = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, tmp.c_str(), hookDir.c_str(),
                                                    drvDir.c_str(), sess.driverName.c_str(), nullptr, nullptr);
            if (!libvulkan_) turnipError = "adrenotools gagal memuat driver Turnip";
        }
        if (!libvulkan_) LOGW("Turnip: %s — memakai driver sistem", turnipError.c_str());
    }
    if (!libvulkan_) libvulkan_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!libvulkan_) { err = "libvulkan.so tidak ditemukan"; return false; }
    auto gipa = (PFN_vkGetInstanceProcAddr)dlsym(libvulkan_, "vkGetInstanceProcAddr");
    if (!gipa) { err = "vkGetInstanceProcAddr tidak ada"; return false; }
    volkInitializeCustom(gipa);

    // --- instance
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "CS:GO Mobile";
    app.applicationVersion = 1;
    app.pEngineName = "csgo-mobile-engine";
    app.apiVersion = VK_API_VERSION_1_1;
    const char* instExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instExt;
    VkResult r = vkCreateInstance(&ici, nullptr, &instance_);
    if (r != VK_SUCCESS) { err = "vkCreateInstance gagal (" + std::to_string(r) + ")"; return false; }
    volkLoadInstance(instance_);

    // --- perangkat fisik
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (n == 0) { err = "tidak ada GPU Vulkan"; return false; }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());
    phys_ = devs[0];
    vkGetPhysicalDeviceProperties(phys_, &props_);
    vkGetPhysicalDeviceMemoryProperties(phys_, &memProps_);
    VkPhysicalDeviceFeatures feats{};
    vkGetPhysicalDeviceFeatures(phys_, &feats);
    gpuName = props_.deviceName;
    char drv[128];
    snprintf(drv, sizeof(drv), "Vulkan %u.%u.%u, driver 0x%08X", VK_API_VERSION_MAJOR(props_.apiVersion),
             VK_API_VERSION_MINOR(props_.apiVersion), VK_API_VERSION_PATCH(props_.apiVersion), props_.driverVersion);
    driverInfo = drv;
    usingTurnip = gpuName.find("Turnip") != std::string::npos || gpuName.find("turnip") != std::string::npos ||
                  (turnipRequested && turnipError.empty());
    LOGI("GPU: %s (%s)%s", gpuName.c_str(), driverInfo.c_str(), usingTurnip ? " [Turnip]" : "");

    // --- antrian
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qf(n);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &n, qf.data());
    queueFamily_ = UINT32_MAX;
    for (uint32_t i = 0; i < n; ++i)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { queueFamily_ = i; break; }
    if (queueFamily_ == UINT32_MAX) { err = "tidak ada antrian grafis"; return false; }

    // --- fitur: BCn & anisotropi
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(phys_, VK_FORMAT_BC3_UNORM_BLOCK, &fp);
    bcSupported = feats.textureCompressionBC && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    anisoFeature_ = feats.samplerAnisotropy;
    maxAniso = anisoFeature_ ? props_.limits.maxSamplerAnisotropy : 1.0f;
    LOGI("BCn: %s, anisotropi maks %.0fx", bcSupported ? "ya" : "tidak (dekode di CPU)", maxAniso);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures en{};
    en.samplerAnisotropy = anisoFeature_ ? VK_TRUE : VK_FALSE;
    en.textureCompressionBC = bcSupported ? VK_TRUE : VK_FALSE;
    const char* devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExt;
    dci.pEnabledFeatures = &en;
    r = vkCreateDevice(phys_, &dci, nullptr, &device_);
    if (r != VK_SUCCESS) { err = "vkCreateDevice gagal (" + std::to_string(r) + ")"; return false; }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    // --- format depth
    for (VkFormat f : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        vkGetPhysicalDeviceFormatProperties(phys_, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depthFormat_ = f; break; }
    }

    // --- MSAA
    msaaSamples = 1;
    if (cfg_.antiAliasing == 2) {
        VkSampleCountFlags sc = props_.limits.framebufferColorSampleCounts & props_.limits.framebufferDepthSampleCounts;
        if (sc & VK_SAMPLE_COUNT_4_BIT) msaaSamples = 4;
        else if (sc & VK_SAMPLE_COUNT_2_BIT) msaaSamples = 2;
        LOGI("MSAA: %dx", msaaSamples);
    }

    // --- pool perintah & sinkronisasi
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = queueFamily_;
    VKCHECK(vkCreateCommandPool(device_, &cpi, nullptr, &cmdPool_));
    VKCHECK(vkCreateCommandPool(device_, &cpi, nullptr, &uploadPool_));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = cmdPool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = kFrames;
    VKCHECK(vkAllocateCommandBuffers(device_, &cai, cmds_));
    cai.commandPool = uploadPool_;
    cai.commandBufferCount = 1;
    VKCHECK(vkAllocateCommandBuffers(device_, &cai, &uploadCmd_));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (int i = 0; i < kFrames; ++i) {
        VKCHECK(vkCreateFence(device_, &fci, nullptr, &fences_[i]));
        VKCHECK(vkCreateSemaphore(device_, &sci, nullptr, &acquireSem_[i]));
    }
    fci.flags = 0;
    VKCHECK(vkCreateFence(device_, &fci, nullptr, &uploadFence_));

    if (!createBuffer(staging_, kStagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true)) {
        err = "gagal membuat buffer staging";
        return false;
    }

    // --- sampler
    VkSamplerCreateInfo smp{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    smp.magFilter = smp.minFilter = VK_FILTER_LINEAR;
    smp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    smp.addressModeU = smp.addressModeV = smp.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    smp.maxLod = VK_LOD_CLAMP_NONE;
    if (anisoFeature_ && cfg_.anisotropy > 1) {
        smp.anisotropyEnable = VK_TRUE;
        smp.maxAnisotropy = std::min((float)cfg_.anisotropy, maxAniso);
    }
    VKCHECK(vkCreateSampler(device_, &smp, nullptr, &texSampler_));
    VkSamplerCreateInfo lm{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    lm.magFilter = lm.minFilter = cfg_.shadowQuality == 1 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    lm.addressModeU = lm.addressModeV = lm.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VKCHECK(vkCreateSampler(device_, &lm, nullptr, &lmSampler_));
    VkSamplerCreateInfo ps = lm;
    ps.magFilter = ps.minFilter = VK_FILTER_LINEAR;
    VKCHECK(vkCreateSampler(device_, &ps, nullptr, &postSampler_));

    // --- layout descriptor & pipeline
    VkDescriptorSetLayoutBinding wb[3]{};
    for (int i = 0; i < 3; ++i) {
        wb[i].binding = i;
        wb[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wb[i].descriptorCount = 1;
        wb[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 3;
    dl.pBindings = wb;
    VKCHECK(vkCreateDescriptorSetLayout(device_, &dl, nullptr, &worldSetLayout_));
    dl.bindingCount = 1;
    VKCHECK(vkCreateDescriptorSetLayout(device_, &dl, nullptr, &postSetLayout_));

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 96};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &worldSetLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    VKCHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &worldLayout_));
    VkPushConstantRange ppc{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    pl.pSetLayouts = &postSetLayout_;
    pl.pPushConstantRanges = &ppc;
    VKCHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &postLayout_));

    worldVs_ = shader(spv_world_vert, sizeof(spv_world_vert));
    worldFs_ = shader(spv_world_frag, sizeof(spv_world_frag));
    postVs_ = shader(spv_post_vert, sizeof(spv_post_vert));
    postFs_ = shader(spv_post_frag, sizeof(spv_post_frag));
    if (!worldVs_ || !worldFs_ || !postVs_ || !postFs_) { err = "gagal membuat shader"; return false; }

    loadPipelineCache();
    if (!createScenePass()) { err = "gagal membuat render pass"; return false; }

    // pool descriptor (material diisi saat uploadWorld)
    VkDescriptorPoolSize psz{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * 8192};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 8192;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &psz;
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    VKCHECK(vkCreateDescriptorPool(device_, &dpi, nullptr, &descPool_));

    // tekstur bawaan
    beginUploads();
    TextureData white;
    white.width = white.height = 4;
    white.mips.push_back({4, 4, std::vector<uint8_t>(64, 255)});
    whiteTex_ = createTexture(white, "putih");
    TextureData miss;
    miss.width = miss.height = 64;
    TextureMip mm;
    mm.w = mm.h = 64;
    mm.data.resize(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            bool c = ((x / 16) + (y / 16)) & 1;
            uint8_t* p = &mm.data[(y * 64 + x) * 4];
            p[0] = c ? 200 : 40; p[1] = c ? 60 : 40; p[2] = c ? 200 : 40; p[3] = 255;
        }
    miss.mips.push_back(mm);
    missingTex_ = createTexture(miss, "hilang");
    return whiteTex_ >= 0 && missingTex_ >= 0;
}

// Afinitas thread render sesuai Mode Performa (inti besar/kecil dibaca dari cpufreq)
void Renderer::applyThreadAffinity() {
    if (cfg_.perfMode == 1) return;
    int ncpu = (int)sysconf(_SC_NPROCESSORS_CONF);
    std::vector<long> freq(ncpu, 0);
    long maxF = 0, minF = LONG_MAX;
    for (int i = 0; i < ncpu; ++i) {
        std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/cpuinfo_max_freq");
        if (f >> freq[i]) { maxF = std::max(maxF, freq[i]); minF = std::min(minF, freq[i]); }
    }
    if (maxF == 0 || maxF == minF) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    int count = 0;
    for (int i = 0; i < ncpu; ++i) {
        bool little = freq[i] == minF;
        if ((cfg_.perfMode == 0 && little) || (cfg_.perfMode == 2 && !little)) { CPU_SET(i, &set); ++count; }
    }
    if (count && sched_setaffinity(0, sizeof(set), &set) == 0) {
        affinityApplied_ = true;
        LOGI("Afinitas thread render: %d inti (%s)", count, cfg_.perfMode == 0 ? "hemat daya" : "performa tinggi");
    }
}

// ================================================================ memori
uint32_t Renderer::memType(uint32_t bits, VkMemoryPropertyFlags props) const {
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool Renderer::allocBlock(const VkMemoryRequirements& req, VkMemoryPropertyFlags props, bool linear,
                          VkDeviceMemory& mem, VkDeviceSize& off) {
    uint32_t type = memType(req.memoryTypeBits, props);
    if (type == UINT32_MAX) return false;
    for (auto& b : blocks_) {
        if (b.type != type || b.linear != linear) continue;
        VkDeviceSize o = (b.used + req.alignment - 1) / req.alignment * req.alignment;
        if (o + req.size <= b.size) { b.used = o + req.size; mem = b.mem; off = o; return true; }
    }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = std::max(kBlockSize, req.size);
    ai.memoryTypeIndex = type;
    Block b{};
    if (vkAllocateMemory(device_, &ai, nullptr, &b.mem) != VK_SUCCESS) return false;
    b.size = ai.allocationSize;
    b.used = req.size;
    b.type = type;
    b.linear = linear;
    blocks_.push_back(b);
    mem = b.mem;
    off = 0;
    return true;
}

bool Renderer::createImage(Image& img, uint32_t w, uint32_t h, uint32_t mips, VkFormat fmt, VkImageUsageFlags usage,
                           VkSampleCountFlagBits samples, bool dedicated, VkImageAspectFlags aspect) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {w, h, 1};
    ii.mipLevels = mips;
    ii.arrayLayers = 1;
    ii.samples = samples;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKCHECK(vkCreateImage(device_, &ii, nullptr, &img.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, img.image, &req);
    VkDeviceMemory mem;
    VkDeviceSize off = 0;
    if (dedicated) {
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        // memori lazily-allocated untuk target MSAA/depth sementara (hemat bandwidth di GPU tile)
        uint32_t t = UINT32_MAX;
        if (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
            t = memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
        if (t == UINT32_MAX) t = memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ai.memoryTypeIndex = t;
        VKCHECK(vkAllocateMemory(device_, &ai, nullptr, &img.dedicated));
        mem = img.dedicated;
    } else if (!allocBlock(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, mem, off)) {
        LOGE("alokasi memori gambar gagal");
        return false;
    }
    VKCHECK(vkBindImageMemory(device_, img.image, mem, off));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {aspect, 0, mips, 0, 1};
    VKCHECK(vkCreateImageView(device_, &vi, nullptr, &img.view));
    img.format = fmt;
    img.w = w;
    img.h = h;
    img.mips = mips;
    return true;
}

void Renderer::destroyImage(Image& img) {
    if (img.view) vkDestroyImageView(device_, img.view, nullptr);
    if (img.image) vkDestroyImage(device_, img.image, nullptr);
    if (img.dedicated) vkFreeMemory(device_, img.dedicated, nullptr);
    img = Image();
}

bool Renderer::createBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool dedicated) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    VKCHECK(vkCreateBuffer(device_, &bi, nullptr, &b.buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, b.buffer, &req);
    VkDeviceMemory mem;
    VkDeviceSize off = 0;
    if (dedicated) {
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memType(req.memoryTypeBits, props);
        if (ai.memoryTypeIndex == UINT32_MAX) return false;
        VKCHECK(vkAllocateMemory(device_, &ai, nullptr, &b.dedicated));
        mem = b.dedicated;
    } else if (!allocBlock(req, props, true, mem, off)) {
        return false;
    }
    VKCHECK(vkBindBufferMemory(device_, b.buffer, mem, off));
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        if (!b.dedicated) return false;
        VKCHECK(vkMapMemory(device_, b.dedicated, 0, VK_WHOLE_SIZE, 0, &b.mapped));
    }
    b.size = size;
    return true;
}

void Renderer::destroyBuffer(Buffer& b) {
    if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
    if (b.dedicated) vkFreeMemory(device_, b.dedicated, nullptr);
    b = Buffer();
}

// ================================================================ upload
void Renderer::beginUploads() {
    if (uploading_) return;
    vkResetCommandPool(device_, uploadPool_, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(uploadCmd_, &bi);
    stagingUsed_ = 0;
    uploading_ = true;
}

void Renderer::flushUploads() {
    if (!uploading_) return;
    vkEndCommandBuffer(uploadCmd_);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &uploadCmd_;
    vkResetFences(device_, 1, &uploadFence_);
    vkQueueSubmit(queue_, 1, &si, uploadFence_);
    vkWaitForFences(device_, 1, &uploadFence_, VK_TRUE, UINT64_MAX);
    for (auto& b : tempStaging_) destroyBuffer(b);
    tempStaging_.clear();
    uploading_ = false;
    stagingUsed_ = 0;
}

// Salin data ke staging; mengembalikan offset (buffer staging_ atau buffer sementara terakhir)
bool Renderer::stage(const void* data, VkDeviceSize size, VkDeviceSize& offOut) {
    VkDeviceSize o = (stagingUsed_ + 15) & ~VkDeviceSize(15);
    if (o + size > kStagingSize) {
        flushUploads();
        beginUploads();
        o = 0;
    }
    if (size > kStagingSize) return false;
    memcpy((uint8_t*)staging_.mapped + o, data, size);
    stagingUsed_ = o + size;
    offOut = o;
    return true;
}

void Renderer::uploadBuffer(Buffer& dst, const void* data, VkDeviceSize size) {
    beginUploads();
    const uint8_t* p = (const uint8_t*)data;
    VkDeviceSize done = 0;
    while (done < size) {
        VkDeviceSize chunk = std::min<VkDeviceSize>(size - done, kStagingSize / 2);
        VkDeviceSize off;
        if (!stage(p + done, chunk, off)) return;
        VkBufferCopy c{off, done, chunk};
        vkCmdCopyBuffer(uploadCmd_, staging_.buffer, dst.buffer, 1, &c);
        done += chunk;
    }
}

int Renderer::createTexture(const TextureData& td, const std::string& debugName) {
    if (td.mips.empty()) return -1;
    VkFormat fmt;
    switch (td.fmt) {
        case TexFmt::BC1: fmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
        case TexFmt::BC2: fmt = VK_FORMAT_BC2_UNORM_BLOCK; break;
        case TexFmt::BC3: fmt = VK_FORMAT_BC3_UNORM_BLOCK; break;
        default: fmt = VK_FORMAT_R8G8B8A8_UNORM; break;
    }
    if (td.fmt != TexFmt::RGBA8 && !bcSupported) return -1;
    Image img;
    uint32_t mips = (uint32_t)td.mips.size();
    if (!createImage(img, (uint32_t)td.mips[0].w, (uint32_t)td.mips[0].h, mips, fmt,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT, false,
                     VK_IMAGE_ASPECT_COLOR_BIT)) {
        LOGW("gagal membuat tekstur %s", debugName.c_str());
        return -1;
    }
    beginUploads();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(uploadCmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    for (uint32_t m = 0; m < mips; ++m) {
        const TextureMip& mip = td.mips[m];
        VkDeviceSize off;
        if (!stage(mip.data.data(), mip.data.size(), off)) { LOGW("tekstur %s terlalu besar", debugName.c_str()); break; }
        VkBufferImageCopy c{};
        c.bufferOffset = off;
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
        c.imageExtent = {(uint32_t)mip.w, (uint32_t)mip.h, 1};
        vkCmdCopyBufferToImage(uploadCmd_, staging_.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        textureBytes += mip.data.size();
    }
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(uploadCmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    textures_.push_back(img);
    return (int)textures_.size() - 1;
}

VkDescriptorSet Renderer::allocSet(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    VkDescriptorSet s = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &ai, &s) != VK_SUCCESS) return VK_NULL_HANDLE;
    return s;
}

void Renderer::writeWorldSet(VkDescriptorSet set, int tex0, int tex1, VkImageView lm) {
    VkDescriptorImageInfo ii[3]{};
    ii[0] = {texSampler_, textures_[tex0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    ii[1] = {texSampler_, textures_[tex1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    ii[2] = {lmSampler_, lm, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[3]{};
    for (int i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(device_, 3, w, 0, nullptr);
}

bool Renderer::uploadWorld(const WorldMesh& wm, const std::vector<int>& baseTex, const std::vector<int>& baseTex2) {
    // lightmap atlas RGBA16F
    if (!createImage(lightmap_, wm.lmWidth, wm.lmHeight, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT, false,
                     VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
    beginUploads();
    {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = lightmap_.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(uploadCmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        // salin per baris agar muat di staging
        size_t rowBytes = (size_t)wm.lmWidth * 8;
        uint32_t rowsPer = (uint32_t)std::max<size_t>(1, (kStagingSize / 2) / rowBytes);
        for (uint32_t y = 0; y < (uint32_t)wm.lmHeight; y += rowsPer) {
            uint32_t rows = std::min<uint32_t>(rowsPer, wm.lmHeight - y);
            VkDeviceSize off;
            if (!stage(&wm.lightmapRGBA16F[(size_t)y * wm.lmWidth * 4], rowBytes * rows, off)) return false;
            VkBufferImageCopy c{};
            c.bufferOffset = off;
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.imageOffset = {0, (int32_t)y, 0};
            c.imageExtent = {(uint32_t)wm.lmWidth, rows, 1};
            vkCmdCopyBufferToImage(uploadCmd_, staging_.buffer, lightmap_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        }
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(uploadCmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    VkDeviceSize vb = wm.vertices.size() * sizeof(WorldVertex), ib = wm.indices.size() * sizeof(uint32_t);
    if (!createBuffer(vbo_, vb, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false) ||
        !createBuffer(ibo_, ib, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false))
        return false;
    uploadBuffer(vbo_, wm.vertices.data(), vb);
    uploadBuffer(ibo_, wm.indices.data(), ib);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    vkCmdPipelineBarrier(uploadCmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    flushUploads();

    // batch + descriptor
    float lmMode = (float)cfg_.shadowQuality;
    for (auto& b : wm.batches) {
        const WorldMaterial& m = wm.materials[b.material];
        BatchGpu g{};
        int t0 = baseTex[b.material] >= 0 ? baseTex[b.material] : missingTex_;
        int t1 = (m.blend2 && baseTex2[b.material] >= 0) ? baseTex2[b.material] : whiteTex_;
        if (m.kind == MatKind::Water && baseTex[b.material] < 0) t0 = whiteTex_;
        g.set = allocSet(worldSetLayout_);
        if (!g.set) { LOGE("descriptor set habis"); return false; }
        writeWorldSet(g.set, t0, t1, lightmap_.view);
        g.firstIndex = b.firstIndex;
        g.indexCount = b.indexCount;
        g.pipeline = (m.kind == MatKind::Translucent || m.kind == MatKind::Water) ? 1 : m.kind == MatKind::Additive ? 2 : 0;
        memcpy(g.tint, m.tint, sizeof(g.tint));
        g.params[0] = m.kind == MatKind::AlphaTest ? std::max(0.01f, m.alphaRef) : 0.0f;
        g.params[1] = (m.blend2 && baseTex2[b.material] >= 0) ? 1.0f : 0.0f;
        g.params[2] = m.unlit ? 0.0f : lmMode;
        g.params[3] = 1.0f;
        batches_.push_back(g);
    }
    LOGI("Upload: %zu tekstur (%.1f MB), %.1f MB geometri, %zu batch", textures_.size(), textureBytes / 1048576.0,
         (vb + ib) / 1048576.0, batches_.size());
    return true;
}

void Renderer::setSky(const int faceTex[6]) {
    // urutan wajah: rt, bk, lf, ft, up, dn (konvensi skybox Quake/Source)
    static const int stToVec[6][3] = {{3, -1, 2}, {-3, 1, 2}, {1, 3, 2}, {-1, -3, 2}, {-2, -1, 3}, {2, -1, -3}};
    std::vector<WorldVertex> v;
    std::vector<uint32_t> idx;
    const float S = 10.0f;
    for (int f = 0; f < 6; ++f) {
        uint32_t base = (uint32_t)v.size();
        const float corners[4][2] = {{-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
        for (auto& c : corners) {
            float b[3] = {c[0] * S, c[1] * S, S};
            WorldVertex wv{};
            for (int j = 0; j < 3; ++j) {
                int k = stToVec[f][j];
                wv.pos[j] = k < 0 ? -b[-k - 1] : b[k - 1];
            }
            wv.uv[0] = (c[0] + 1) * 0.5f;
            wv.uv[1] = 1.0f - (c[1] + 1) * 0.5f;
            wv.lm[0] = wv.lm[1] = 0.5f;
            wv.alpha = 1;
            v.push_back(wv);
        }
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        skySets_[f] = allocSet(worldSetLayout_);
        int t = faceTex[f] >= 0 ? faceTex[f] : whiteTex_;
        if (skySets_[f]) writeWorldSet(skySets_[f], t, whiteTex_, textures_[whiteTex_].view);
    }
    if (!createBuffer(skyVbo_, v.size() * sizeof(WorldVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false) ||
        !createBuffer(skyIbo_, idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false))
        return;
    uploadBuffer(skyVbo_, v.data(), v.size() * sizeof(WorldVertex));
    uploadBuffer(skyIbo_, idx.data(), idx.size() * 4);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    vkCmdPipelineBarrier(uploadCmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    flushUploads();
    hasSky_ = true;
}

// ================================================================ pipeline
VkShaderModule Renderer::shader(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device_, &ci, nullptr, &m);
    return m;
}

void Renderer::loadPipelineCache() {
    std::vector<char> data;
    std::string path = sess_.cacheDir + "/pipeline_cache.bin";
    if (cfg_.shaderCache) {
        std::ifstream f(path, std::ios::binary);
        if (f) data.assign(std::istreambuf_iterator<char>(f), {});
        // validasi header cache (vendor/device/UUID harus sama, mis. ganti driver Turnip <-> sistem)
        if (data.size() >= 32) {
            uint32_t vendor, devId;
            memcpy(&vendor, &data[8], 4);
            memcpy(&devId, &data[12], 4);
            if (vendor != props_.vendorID || devId != props_.deviceID || memcmp(&data[16], props_.pipelineCacheUUID, 16) != 0) {
                LOGI("Cache shader dari driver lain — dibuat ulang");
                data.clear();
            }
        } else data.clear();
    }
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    ci.initialDataSize = data.size();
    ci.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &ci, nullptr, &pipelineCache_) != VK_SUCCESS) {
        ci.initialDataSize = 0;
        ci.pInitialData = nullptr;
        vkCreatePipelineCache(device_, &ci, nullptr, &pipelineCache_);
    }
    LOGI("Cache shader: %s (%zu byte dimuat)", cfg_.shaderCache ? "aktif" : "nonaktif", data.size());
}

void Renderer::savePipelineCache() {
    if (!cfg_.shaderCache || !pipelineCache_) return;
    size_t sz = 0;
    if (vkGetPipelineCacheData(device_, pipelineCache_, &sz, nullptr) != VK_SUCCESS || !sz) return;
    std::vector<char> d(sz);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &sz, d.data()) != VK_SUCCESS) return;
    mkdir(sess_.cacheDir.c_str(), 0775);
    std::ofstream f(sess_.cacheDir + "/pipeline_cache.bin", std::ios::binary);
    f.write(d.data(), (std::streamsize)sz);
}

VkPipeline Renderer::makePipeline(VkRenderPass pass, VkSampleCountFlagBits samples, int blend, bool depthTest, bool depthWrite,
                                  VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout, bool vertexInput) {
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkVertexInputBindingDescription vb{0, sizeof(WorldVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, 20},
        {3, 0, VK_FORMAT_R32_SFLOAT, 28}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (vertexInput) {
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vb;
        vi.vertexAttributeDescriptionCount = 4;
        vi.pVertexAttributeDescriptions = va;
    }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = depthTest;
    ds.depthWriteEnable = depthWrite;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cb{};
    cb.colorWriteMask = 0xF;
    if (blend == 1) {
        cb.blendEnable = VK_TRUE;
        cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb.colorBlendOp = VK_BLEND_OP_ADD;
        cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (blend == 2) {
        cb.blendEnable = VK_TRUE;
        cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cb.colorBlendOp = VK_BLEND_OP_ADD;
        cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cb.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount = 1;
    cbs.pAttachments = &cb;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2;
    pci.pStages = st;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cbs;
    pci.pDynamicState = &dy;
    pci.layout = layout;
    pci.renderPass = pass;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pci, nullptr, &p) != VK_SUCCESS) LOGE("gagal membuat pipeline");
    return p;
}

bool Renderer::createScenePass() {
    VkSampleCountFlagBits samples = (VkSampleCountFlagBits)msaaSamples;
    std::vector<VkAttachmentDescription> att;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentDescription color{};
    color.format = VK_FORMAT_R8G8B8A8_UNORM;
    color.samples = samples;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = msaaSamples > 1 ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = msaaSamples > 1 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    att.push_back(color);
    VkAttachmentDescription depth = color;
    depth.format = depthFormat_;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    att.push_back(depth);
    if (msaaSamples > 1) {
        VkAttachmentDescription res = color;
        res.samples = VK_SAMPLE_COUNT_1_BIT;
        res.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        res.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        res.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        att.push_back(res);
    }
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &colorRef;
    sp.pDepthStencilAttachment = &depthRef;
    if (msaaSamples > 1) sp.pResolveAttachments = &resolveRef;
    VkSubpassDependency dep[2]{};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0;
    dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = (uint32_t)att.size();
    rp.pAttachments = att.data();
    rp.subpassCount = 1;
    rp.pSubpasses = &sp;
    rp.dependencyCount = 2;
    rp.pDependencies = dep;
    VKCHECK(vkCreateRenderPass(device_, &rp, nullptr, &scenePass_));
    worldPipes_.resize(4);
    worldPipes_[0] = makePipeline(scenePass_, samples, 0, true, true, worldVs_, worldFs_, worldLayout_, true);
    worldPipes_[1] = makePipeline(scenePass_, samples, 1, true, false, worldVs_, worldFs_, worldLayout_, true);
    worldPipes_[2] = makePipeline(scenePass_, samples, 2, true, false, worldVs_, worldFs_, worldLayout_, true);
    worldPipes_[3] = makePipeline(scenePass_, samples, 0, false, false, worldVs_, worldFs_, worldLayout_, true);
    for (auto p : worldPipes_) if (!p) return false;
    return true;
}

// ================================================================ swapchain
bool Renderer::attachWindow(ANativeWindow* win, std::string& err) {
    window_ = win;
    VkAndroidSurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    si.window = win;
    if (vkCreateAndroidSurfaceKHR(instance_, &si, nullptr, &surface_) != VK_SUCCESS) { err = "gagal membuat surface"; return false; }
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(phys_, queueFamily_, surface_, &present);
    if (!present) { err = "antrian tidak mendukung present"; return false; }
    return createSwapchain(err);
}

bool Renderer::createSwapchain(std::string& err) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps);
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &n, fmts.data());
    VkSurfaceFormatKHR fmt = fmts.empty() ? VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : fmts[0];
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) { fmt = f; break; }
    swapFormat_ = fmt.format;

    vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &n, modes.data());
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    if (!cfg_.vsync) {
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) pm = m;
        if (pm == VK_PRESENT_MODE_FIFO_KHR)
            for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) pm = m;
    }

    swapExtent_ = caps.currentExtent;
    if (swapExtent_.width == 0xFFFFFFFFu) {
        swapExtent_.width = (uint32_t)ANativeWindow_getWidth(window_);
        swapExtent_.height = (uint32_t)ANativeWindow_getHeight(window_);
    }
    if (swapExtent_.width == 0 || swapExtent_.height == 0) { err = "ukuran surface 0"; return false; }
    uint32_t count = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount) count = std::min(count + 1, caps.maxImageCount);
    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = surface_;
    sc.minImageCount = count;
    sc.imageFormat = fmt.format;
    sc.imageColorSpace = fmt.colorSpace;
    sc.imageExtent = swapExtent_;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Rotasi diserahkan ke compositor bila transform non-identitas (aplikasi landscape)
    sc.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    sc.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                                                           : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    sc.presentMode = pm;
    sc.clipped = VK_TRUE;
    VkResult r = vkCreateSwapchainKHR(device_, &sc, nullptr, &swapchain_);
    if (r != VK_SUCCESS) { err = "vkCreateSwapchainKHR gagal (" + std::to_string(r) + ")"; return false; }
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapImages_.data());
    outW = swapExtent_.width;
    outH = swapExtent_.height;
    LOGI("Swapchain %ux%u, %u gambar, present %s", outW, outH, n,
         pm == VK_PRESENT_MODE_FIFO_KHR ? "FIFO (V-Sync)" : pm == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "IMMEDIATE");

    // post pass ke swapchain
    if (!postPass_) {
        VkAttachmentDescription a{};
        a.format = swapFormat_;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1;
        rp.pAttachments = &a;
        rp.subpassCount = 1;
        rp.pSubpasses = &sp;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;
        if (vkCreateRenderPass(device_, &rp, nullptr, &postPass_) != VK_SUCCESS) { err = "render pass layar gagal"; return false; }
        postPipe_ = makePipeline(postPass_, VK_SAMPLE_COUNT_1_BIT, 0, false, false, postVs_, postFs_, postLayout_, false);
        if (!postPipe_) { err = "pipeline layar gagal"; return false; }
    }
    swapViews_.resize(n);
    postFbs_.resize(n);
    renderDone_.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = swapImages_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapFormat_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &vi, nullptr, &swapViews_[i]) != VK_SUCCESS) { err = "image view gagal"; return false; }
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = postPass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &swapViews_[i];
        fb.width = outW;
        fb.height = outH;
        fb.layers = 1;
        if (vkCreateFramebuffer(device_, &fb, nullptr, &postFbs_[i]) != VK_SUCCESS) { err = "framebuffer gagal"; return false; }
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device_, &sci, nullptr, &renderDone_[i]);
    }

    // area tampilan sesuai Aspect Ratio
    float target = cfg_.aspect == 1 ? 16.f / 9.f : cfg_.aspect == 2 ? 16.f / 10.f : (float)outW / outH;
    uint32_t vw = outW, vh = outH;
    if ((float)outW / outH > target) vw = (uint32_t)std::lround(outH * target);
    else vh = (uint32_t)std::lround(outW / target);
    viewport_.offset = {(int32_t)(outW - vw) / 2, (int32_t)(outH - vh) / 2};
    viewport_.extent = {vw, vh};
    if (!createSceneTargets()) { err = "target render gagal dibuat"; return false; }
    return true;
}

bool Renderer::createSceneTargets() {
    float s = std::max(0.25f, cfg_.renderScale / 100.0f);
    uint32_t maxDim = props_.limits.maxImageDimension2D;
    sceneW = std::min(maxDim, std::max(64u, (uint32_t)std::lround(viewport_.extent.width * s)));
    sceneH = std::min(maxDim, std::max(64u, (uint32_t)std::lround(viewport_.extent.height * s)));
    VkSampleCountFlagBits samples = (VkSampleCountFlagBits)msaaSamples;
    if (!createImage(sceneColor_, sceneW, sceneH, 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT, true,
                     VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
    if (msaaSamples > 1 &&
        !createImage(sceneMsaa_, sceneW, sceneH, 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, samples, true,
                     VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
    VkImageAspectFlags da = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (!createImage(sceneDepth_, sceneW, sceneH, 1, depthFormat_,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, samples, true, da))
        return false;
    std::vector<VkImageView> views;
    if (msaaSamples > 1) views = {sceneMsaa_.view, sceneDepth_.view, sceneColor_.view};
    else views = {sceneColor_.view, sceneDepth_.view};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = scenePass_;
    fb.attachmentCount = (uint32_t)views.size();
    fb.pAttachments = views.data();
    fb.width = sceneW;
    fb.height = sceneH;
    fb.layers = 1;
    VKCHECK(vkCreateFramebuffer(device_, &fb, nullptr, &sceneFb_));
    if (!postSet_) postSet_ = allocSet(postSetLayout_);
    VkDescriptorImageInfo ii{postSampler_, sceneColor_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = postSet_;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    LOGI("Render 3D %ux%u (skala %d%%), tampilan %ux%u", sceneW, sceneH, cfg_.renderScale, viewport_.extent.width, viewport_.extent.height);
    return true;
}

void Renderer::destroySceneTargets() {
    if (sceneFb_) vkDestroyFramebuffer(device_, sceneFb_, nullptr);
    sceneFb_ = VK_NULL_HANDLE;
    destroyImage(sceneColor_);
    destroyImage(sceneMsaa_);
    destroyImage(sceneDepth_);
}

void Renderer::destroySwapchain() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    destroySceneTargets();
    for (auto f : postFbs_) vkDestroyFramebuffer(device_, f, nullptr);
    for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
    for (auto s : renderDone_) vkDestroySemaphore(device_, s, nullptr);
    postFbs_.clear();
    swapViews_.clear();
    renderDone_.clear();
    swapImages_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void Renderer::detachWindow() {
    destroySwapchain();
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    window_ = nullptr;
}

// ================================================================ frame
bool Renderer::drawFrame(const Vec3& eye, float pitchDeg, float yawDeg) {
    if (!swapchain_) return true;
    vkWaitForFences(device_, 1, &fences_[frame_], VK_TRUE, UINT64_MAX);
    uint32_t img = 0;
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquireSem_[frame_], VK_NULL_HANDLE, &img);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        std::string e;
        destroySwapchain();
        return createSwapchain(e);
    }
    if (r == VK_ERROR_DEVICE_LOST) { LOGE("GPU device lost"); return false; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return true;
    vkResetFences(device_, 1, &fences_[frame_]);

    VkCommandBuffer cmd = cmds_[frame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    // matriks: FOV horizontal 90 pada 4:3 -> FOV vertikal tetap (Hor+ untuk layar lebar)
    float aspect = (float)sceneW / (float)sceneH;
    float vfov = 2.0f * std::atan(std::tan(cfg_.fovDeg * (float)M_PI / 360.f) * 0.75f);
    Mat4 proj = perspectiveVk(vfov, aspect, 4.0f, 16384.0f);
    Mat4 view = viewSource(eye, pitchDeg, yawDeg);
    Mat4 vpm = proj * view;
    Mat4 skyView = viewSource(Vec3(), pitchDeg, yawDeg);
    Mat4 skyVp = proj * skyView;

    // --- pass adegan 3D
    VkClearValue clears[3];
    clears[0].color = {{0.35f, 0.45f, 0.6f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    clears[2].color = clears[0].color;
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = scenePass_;
    rb.framebuffer = sceneFb_;
    rb.renderArea = {{0, 0}, {sceneW, sceneH}};
    rb.clearValueCount = msaaSamples > 1 ? 3 : 2;
    rb.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)sceneW, (float)sceneH, 0, 1};
    VkRect2D sc{{0, 0}, {sceneW, sceneH}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    struct PC { float mvp[16]; float tint[4]; float params[4]; } pc;
    VkDeviceSize zero = 0;
    if (hasSky_ && skyVbo_.buffer) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldPipes_[3]);
        vkCmdBindVertexBuffers(cmd, 0, 1, &skyVbo_.buffer, &zero);
        vkCmdBindIndexBuffer(cmd, skyIbo_.buffer, 0, VK_INDEX_TYPE_UINT32);
        memcpy(pc.mvp, skyVp.m, 64);
        pc.tint[0] = pc.tint[1] = pc.tint[2] = pc.tint[3] = 1;
        pc.params[0] = pc.params[1] = pc.params[2] = 0;
        pc.params[3] = 1;
        vkCmdPushConstants(cmd, worldLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        for (int f = 0; f < 6; ++f) {
            if (!skySets_[f]) continue;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldLayout_, 0, 1, &skySets_[f], 0, nullptr);
            vkCmdDrawIndexed(cmd, 6, 1, f * 6, 0, 0);
        }
    }
    if (vbo_.buffer) {
        vkCmdBindVertexBuffers(cmd, 0, 1, &vbo_.buffer, &zero);
        vkCmdBindIndexBuffer(cmd, ibo_.buffer, 0, VK_INDEX_TYPE_UINT32);
        memcpy(pc.mvp, vpm.m, 64);
        int boundPipe = -1;
        for (auto& b : batches_) {
            if (b.pipeline != boundPipe) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldPipes_[b.pipeline]);
                boundPipe = b.pipeline;
            }
            memcpy(pc.tint, b.tint, 16);
            memcpy(pc.params, b.params, 16);
            vkCmdPushConstants(cmd, worldLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldLayout_, 0, 1, &b.set, 0, nullptr);
            vkCmdDrawIndexed(cmd, b.indexCount, 1, b.firstIndex, 0, 0);
        }
    }
    vkCmdEndRenderPass(cmd);

    // --- pass layar (skala + FXAA + letterbox)
    VkClearValue black;
    black.color = {{0, 0, 0, 1}};
    rb.renderPass = postPass_;
    rb.framebuffer = postFbs_[img];
    rb.renderArea = {{0, 0}, swapExtent_};
    rb.clearValueCount = 1;
    rb.pClearValues = &black;
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport pvp{(float)viewport_.offset.x, (float)viewport_.offset.y, (float)viewport_.extent.width, (float)viewport_.extent.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &pvp);
    vkCmdSetScissor(cmd, 0, 1, &viewport_);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipe_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &postSet_, 0, nullptr);
    float p0[4] = {1.0f / sceneW, 1.0f / sceneH, cfg_.antiAliasing == 1 ? 1.0f : 0.0f, 0};
    vkCmdPushConstants(cmd, postLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, p0);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquireSem_[frame_];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderDone_[img];
    r = vkQueueSubmit(queue_, 1, &si, fences_[frame_]);
    if (r == VK_ERROR_DEVICE_LOST) { LOGE("GPU device lost saat submit"); return false; }

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderDone_[img];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &img;
    r = vkQueuePresentKHR(queue_, &pi);
    frame_ = (frame_ + 1) % kFrames;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        std::string e;
        destroySwapchain();
        return createSwapchain(e);
    }
    return r != VK_ERROR_DEVICE_LOST;
}

// ================================================================ shutdown
void Renderer::shutdown() {
    if (!device_) {
        if (instance_) vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
        return;
    }
    vkDeviceWaitIdle(device_);
    savePipelineCache();
    detachWindow();
    for (auto& t : textures_) destroyImage(t);
    textures_.clear();
    destroyImage(lightmap_);
    destroyBuffer(vbo_);
    destroyBuffer(ibo_);
    destroyBuffer(skyVbo_);
    destroyBuffer(skyIbo_);
    destroyBuffer(staging_);
    for (auto& b : blocks_) vkFreeMemory(device_, b.mem, nullptr);
    blocks_.clear();
    for (auto p : worldPipes_) if (p) vkDestroyPipeline(device_, p, nullptr);
    worldPipes_.clear();
    if (postPipe_) vkDestroyPipeline(device_, postPipe_, nullptr);
    if (scenePass_) vkDestroyRenderPass(device_, scenePass_, nullptr);
    if (postPass_) vkDestroyRenderPass(device_, postPass_, nullptr);
    for (auto m : {worldVs_, worldFs_, postVs_, postFs_}) if (m) vkDestroyShaderModule(device_, m, nullptr);
    if (descPool_) vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (worldLayout_) vkDestroyPipelineLayout(device_, worldLayout_, nullptr);
    if (postLayout_) vkDestroyPipelineLayout(device_, postLayout_, nullptr);
    if (worldSetLayout_) vkDestroyDescriptorSetLayout(device_, worldSetLayout_, nullptr);
    if (postSetLayout_) vkDestroyDescriptorSetLayout(device_, postSetLayout_, nullptr);
    for (auto s : {texSampler_, lmSampler_, postSampler_}) if (s) vkDestroySampler(device_, s, nullptr);
    if (pipelineCache_) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
    for (int i = 0; i < kFrames; ++i) {
        if (fences_[i]) vkDestroyFence(device_, fences_[i], nullptr);
        if (acquireSem_[i]) vkDestroySemaphore(device_, acquireSem_[i], nullptr);
    }
    if (uploadFence_) vkDestroyFence(device_, uploadFence_, nullptr);
    if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);
    if (uploadPool_) vkDestroyCommandPool(device_, uploadPool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
}

} // namespace cs
