#ifndef __Application_h__
#define __Application_h__

#ifdef __EMSCRIPTEN__
// Emscripten / Dawn WebGPU
// NOTE, Parag, the default webgpu.h that gets included with emscripten is not compatible with the lib file it provides somehow
// This particular header file is compatible 
#include "C:\emscripten\emsdk\upstream\emscripten\cache\ports\emdawnwebgpu\emdawnwebgpu_pkg\webgpu\include\webgpu\webgpu.h"
#else
// Includes
#ifdef WEBGPU_BACKEND_WGPU
#include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU

#include <webgpu/webgpu.h>
#endif

#include <utility>

#ifndef SET_WGPU_LABEL
#ifdef __EMSCRIPTEN__
#define SET_WGPU_LABEL(desc, txt)                            \
    WGPUStringView labelView_##desc{};                       \
    labelView_##desc.data = txt;                             \
    labelView_##desc.length = strlen(labelView_##desc.data); \
    (desc).label = labelView_##desc;
#else
#define SET_WGPU_LABEL(desc, txt) \
    (desc).label = txt;
#endif
#endif

class Application
{
public:

    Application();
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator = (const Application&) = delete;

    Application(Application&&) = delete;
    Application& operator = (Application&&) = delete;

    // Initialize everything and return true if it went all right
    bool Initialize();

    // Uninitialize everything that was initialized
    void Terminate();

    // Draw a frame and handle events
    void MainLoop();

    // Return true as long as the main loop should keep on running
    bool IsRunning();

private:

    bool GetInstance();
    bool GetSurface();
    bool GetAdapter();
    bool GetFeaturesAndProperties();
    bool GetDevice();
    bool GetQueue();
    bool LoadShaders();
    bool CreatePipeline();
    bool CreateUniformBuffer();
    bool CreateVertexBuffer();
    bool CreateIndexBuffer();
    void DestroyBuffer(WGPUBuffer& Buffer);

    void GetNextSurfaceViewData(std::pair<WGPUSurfaceTexture, WGPUTextureView>& SurfaceViewData);

    bool m_IsFullyInitialized;

    uint32_t m_ScreenWidth;
    uint32_t m_ScreenHeight;

    WGPULimits m_AdopterLimits;
    WGPULimits m_DeviceLimits;

    struct GLFWwindow* m_Window;

    WGPUInstance m_Instance;

    WGPUSurface m_Surface;

    WGPUAdapter m_Adapter;

    WGPUDevice m_Device;

    WGPUQueue m_Queue;

    WGPUBindGroupLayout m_BindGroupLayout;
    WGPUPipelineLayout m_PipelineLayout;

    WGPUBindGroup m_BindGroup;

    WGPURenderPipeline m_Pipeline;

    WGPUTextureFormat m_SurfaceFormat = WGPUTextureFormat_Undefined;

    WGPUShaderModule m_ShaderModule;

    WGPUVertexBufferLayout m_VertexBufferLayout;

    uint32_t m_VertexBufferSize;
    WGPUBuffer m_Buffer1;

    uint32_t m_IndexBufferSize;
    WGPUBuffer m_IndexBuffer;

    uint32_t m_ConstantUniformBufferSize;
    uint32_t m_ConstantUniformBufferStride;
    uint32_t m_DynamicsUniformBufferSize;
    uint32_t m_DynamicsUniformBufferStride;
    WGPUBuffer m_UniformBuffer;

    WGPUTexture m_DepthTexture;
    WGPUTextureView m_DepthTextureView;
};


#endif //