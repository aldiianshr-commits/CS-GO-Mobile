// Renderer Vulkan untuk Adreno 650 (driver sistem Qualcomm atau Turnip/Mesa via adrenotools).
#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include "volk.h"
#include <android/native_window.h>
#include <string>
#include <vector>
#include "../format/vtf.h"
#include "../game/config.h"
#include "../game/world.h"

namespace cs {

class Renderer {
public:
    // Muat driver Vulkan, buat instance & device. Tidak butuh window.
    bool init(const GraphicsConfig& cfg, const SessionConfig& sess, std::string& err);
    bool attachWindow(ANativeWindow* win, std::string& err);
    void detachWindow();
    bool hasSurface() const { return swapchain_ != VK_NULL_HANDLE; }

    int createTexture(const TextureData& td, const std::string& debugName);
    int whiteTexture() const { return whiteTex_; }
    int missingTexture() const { return missingTex_; }
    bool uploadWorld(const WorldMesh& wm, const std::vector<int>& baseTex, const std::vector<int>& baseTex2);
    void setSky(const int faceTex[6]);
    void flushUploads();

    // Gambar satu frame; false jika terjadi galat fatal (device lost)
    bool drawFrame(const Vec3& eye, float pitchDeg, float yawDeg);
    void shutdown();

    // info
    std::string gpuName, driverInfo;
    bool usingTurnip = false, turnipRequested = false, bcSupported = false;
    std::string turnipError;
    float maxAniso = 1.0f;
    int msaaSamples = 1;
    uint32_t sceneW = 0, sceneH = 0, outW = 0, outH = 0;
    size_t textureBytes = 0;

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory dedicated = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t w = 0, h = 0, mips = 1;
    };
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory dedicated = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };
    struct Block {
        VkDeviceMemory mem;
        VkDeviceSize size, used;
        uint32_t type;
        bool linear;
    };
    struct BatchGpu {
        VkDescriptorSet set;
        uint32_t firstIndex, indexCount;
        int pipeline;
        float tint[4];
        float params[4];
    };

    uint32_t memType(uint32_t bits, VkMemoryPropertyFlags props) const;
    bool allocBlock(const VkMemoryRequirements& req, VkMemoryPropertyFlags props, bool linear, VkDeviceMemory& mem, VkDeviceSize& off);
    bool createImage(Image& img, uint32_t w, uint32_t h, uint32_t mips, VkFormat fmt, VkImageUsageFlags usage,
                     VkSampleCountFlagBits samples, bool dedicated, VkImageAspectFlags aspect);
    void destroyImage(Image& img);
    bool createBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool dedicated);
    void destroyBuffer(Buffer& b);
    bool stage(const void* data, VkDeviceSize size, VkDeviceSize& offOut);
    void beginUploads();
    void uploadBuffer(Buffer& dst, const void* data, VkDeviceSize size);

    bool createSwapchain(std::string& err);
    void destroySwapchain();
    bool createScenePass();
    bool createSceneTargets();
    void destroySceneTargets();
    bool createPipelines();
    VkPipeline makePipeline(VkRenderPass pass, VkSampleCountFlagBits samples, int blend, bool depthTest, bool depthWrite,
                            VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout, bool vertexInput);
    VkShaderModule shader(const uint32_t* code, size_t bytes);
    void loadPipelineCache();
    void savePipelineCache();
    VkDescriptorSet allocSet(VkDescriptorSetLayout layout);
    void writeWorldSet(VkDescriptorSet set, int tex0, int tex1, VkImageView lm);
    void applyThreadAffinity();

    GraphicsConfig cfg_;
    SessionConfig sess_;
    void* libvulkan_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    VkPhysicalDeviceMemoryProperties memProps_{};
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    bool anisoFeature_ = false;

    ANativeWindow* window_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent_{};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    std::vector<VkFramebuffer> postFbs_;
    std::vector<VkSemaphore> renderDone_;
    VkRect2D viewport_{};

    VkRenderPass scenePass_ = VK_NULL_HANDLE, postPass_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    Image sceneColor_, sceneMsaa_, sceneDepth_;
    VkFramebuffer sceneFb_ = VK_NULL_HANDLE;
    VkDescriptorSet postSet_ = VK_NULL_HANDLE;

    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout worldSetLayout_ = VK_NULL_HANDLE, postSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout worldLayout_ = VK_NULL_HANDLE, postLayout_ = VK_NULL_HANDLE;
    std::vector<VkPipeline> worldPipes_; // 0 opaque, 1 translucent, 2 additive, 3 sky
    VkPipeline postPipe_ = VK_NULL_HANDLE;
    VkShaderModule worldVs_ = VK_NULL_HANDLE, worldFs_ = VK_NULL_HANDLE, postVs_ = VK_NULL_HANDLE, postFs_ = VK_NULL_HANDLE;
    VkSampler texSampler_ = VK_NULL_HANDLE, lmSampler_ = VK_NULL_HANDLE, postSampler_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    static constexpr int kFrames = 2;
    VkCommandBuffer cmds_[kFrames]{};
    VkFence fences_[kFrames]{};
    VkSemaphore acquireSem_[kFrames]{};
    int frame_ = 0;

    VkCommandPool uploadPool_ = VK_NULL_HANDLE;
    VkCommandBuffer uploadCmd_ = VK_NULL_HANDLE;
    VkFence uploadFence_ = VK_NULL_HANDLE;
    Buffer staging_;
    VkDeviceSize stagingUsed_ = 0;
    bool uploading_ = false;
    std::vector<Buffer> tempStaging_;

    std::vector<Block> blocks_;
    std::vector<Image> textures_;
    int whiteTex_ = -1, missingTex_ = -1;
    Image lightmap_;
    Buffer vbo_, ibo_, skyVbo_, skyIbo_;
    std::vector<BatchGpu> batches_;
    VkDescriptorSet skySets_[6]{};
    bool hasSky_ = false;
    bool affinityApplied_ = false;
};

} // namespace cs
