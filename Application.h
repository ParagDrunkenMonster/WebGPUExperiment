#ifndef __Application_h__
#define __Application_h__

// Includes
#ifdef WEBGPU_BACKEND_WGPU
#include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU

#include <webgpu/webgpu.h>

#include <utility>

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
    std::pair<WGPUSurfaceTexture, WGPUTextureView> GetNextSurfaceViewData();

    bool m_IsFullyInitialized;

    struct GLFWwindow* m_Window;

    WGPUInstance m_Instance;

    WGPUSurface m_Surface;

    WGPUAdapter m_Adapter;

    WGPUDevice m_Device;

    WGPUQueue m_Queue;
};


#endif //