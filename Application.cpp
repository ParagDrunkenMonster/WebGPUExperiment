#include "Application.h"

#include <iostream>


#include <cassert>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <thread>
#endif // __EMSCRIPTEN__

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

namespace
{
#ifdef __EMSCRIPTEN__
    WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const*)// options) 
#else
    WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const* options) 
#endif
    {
        // A simple structure holding the local information shared with the
        // onAdapterRequestEnded callback.
        struct UserData 
        {
            WGPUAdapter adapter = nullptr;
            bool requestEnded = false;
        };
        UserData userData;

        std::cout << "Requesting adapter started ..." << std::endl;
       
#ifdef __EMSCRIPTEN__

        auto onDeviceRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, WGPU_NULLABLE void* userdata1, WGPU_NULLABLE void*)
        {
            std::cout << "Adopter request ended callback... " << (message.length > 0 ? message.data : "") << std::endl;
            UserData& userData = *reinterpret_cast<UserData*>(userdata1);
            if (status == WGPURequestAdapterStatus_Success)
            {
                userData.adapter = adapter;
            }
            userData.requestEnded = true;
        };

        WGPURequestAdapterCallbackInfo callbackInfo;
        callbackInfo.nextInChain = nullptr;
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = onDeviceRequestEnded;
        callbackInfo.userdata1 = &userData;
        callbackInfo.userdata2 = nullptr;


        WGPUFuture future = wgpuInstanceRequestAdapter(instance, nullptr, callbackInfo);
        std::cout << "Requesting adapter ended..." << std::endl;

        WGPUFutureWaitInfo waitInfo{};
        waitInfo.future = future;
        waitInfo.completed = false;

        while (wgpuInstanceWaitAny(instance, 1, &waitInfo, 0) != WGPUWaitStatus::WGPUWaitStatus_Success)
        {
            std::cout << "Waiting for adapter..." << std::endl;
            emscripten_sleep(100); // Or just return control to browser
        }
#else
        auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const* message, void* pUserData)
        {
            std::cout << "Adopter request ended callback... " << (message ? message : "") << std::endl;
            UserData& userData = *reinterpret_cast<UserData*>(pUserData);
            if (status == WGPURequestAdapterStatus_Success)
            {
                userData.adapter = adapter;
            }
            userData.requestEnded = true;
        };

        // Call to the WebGPU request adapter procedure
        wgpuInstanceRequestAdapter(instance, options, onAdapterRequestEnded, (void*)&userData);
        std::cout << "Requesting adapter ended..." << std::endl;

        while (!userData.requestEnded) 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#endif // __EMSCRIPTEN__

        


        return userData.adapter;
    }

    
#ifdef __EMSCRIPTEN__
    WGPUDevice requestDeviceSync(WGPUInstance instance, WGPUAdapter adapter, WGPUDeviceDescriptor const* descriptor)
#else
    WGPUDevice requestDeviceSync(WGPUInstance, WGPUAdapter adapter, WGPUDeviceDescriptor const* descriptor)
#endif
    {
        struct UserData
        {
            WGPUDevice device = nullptr;
            bool requestEnded = false;
        };

        UserData userData;

        

#ifdef __EMSCRIPTEN__
       
        auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, WGPU_NULLABLE void* userdata1, WGPU_NULLABLE void*)
        {
            std::cout << "Device request ended callback... " << (message.length > 0 ? message.data : "") << std::endl;

            UserData& userData = *reinterpret_cast<UserData*>(userdata1);
            if (status == WGPURequestDeviceStatus_Success)
            {
                userData.device = device;
            }

            userData.requestEnded = true;
        };

        WGPURequestDeviceCallbackInfo callbackInfo;
        callbackInfo.nextInChain = nullptr;
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = onDeviceRequestEnded;
        callbackInfo.userdata1 = &userData;
        callbackInfo.userdata2 = nullptr;

        WGPUFuture future = wgpuAdapterRequestDevice(adapter, descriptor, callbackInfo);

        WGPUFutureWaitInfo waitInfo{};
        waitInfo.future = future;
        waitInfo.completed = false;

        while (wgpuInstanceWaitAny(instance, 1, &waitInfo, 0) != WGPUWaitStatus::WGPUWaitStatus_Success)
        {
            std::cout << "Waiting for device..." << std::endl;
            emscripten_sleep(100); // Or just return control to browser
        }
#else
        auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, char const* message, void* pUserData)
        {
            std::cout << "Device request ended callback... " << (message ? message : "") << std::endl;

            UserData& userData = *reinterpret_cast<UserData*>(pUserData);
            if (status == WGPURequestDeviceStatus_Success)
            {
                userData.device = device;
            }

            userData.requestEnded = true;
        };

        wgpuAdapterRequestDevice(adapter, descriptor, onDeviceRequestEnded, (void*)&userData);

        while (!userData.requestEnded) 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#endif // __EMSCRIPTEN__

        assert(userData.requestEnded);

        return userData.device;
    }

    // We also add an inspect device function:
    void inspectDevice(WGPUDevice device) 
    {
#ifndef __EMSCRIPTEN__
        std::vector<WGPUFeatureName> features;
        size_t featureCount = wgpuDeviceEnumerateFeatures(device, nullptr);
        features.resize(featureCount);
        wgpuDeviceEnumerateFeatures(device, features.data());

        std::cout << "Device features:" << std::endl;
        std::cout << std::hex;
        for (auto f : features) 
        {
            std::cout << " - 0x" << f << std::endl;
        }
        std::cout << std::dec;
#endif

        

#ifdef __EMSCRIPTEN__
        WGPULimits limits;
        limits.nextInChain = nullptr;

        limits.minStorageBufferOffsetAlignment = 256;
        limits.minUniformBufferOffsetAlignment = 256;

        const bool success = wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success;

        if (success)
        {
            std::cout << "Device limits:" << std::endl;
            std::cout << " - maxTextureDimension1D: " << limits.maxTextureDimension1D << std::endl;
            std::cout << " - maxTextureDimension2D: " << limits.maxTextureDimension2D << std::endl;
            std::cout << " - maxTextureDimension3D: " << limits.maxTextureDimension3D << std::endl;
            std::cout << " - maxTextureArrayLayers: " << limits.maxTextureArrayLayers << std::endl;
            // { { Extra device limits } }
        }
#else
        WGPUSupportedLimits limits = {};
        limits.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
        const bool success = wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success;
#else
        const bool success = wgpuDeviceGetLimits(device, &limits);
#endif

        if (success)
        {
            std::cout << "Device limits:" << std::endl;
            std::cout << " - maxTextureDimension1D: " << limits.limits.maxTextureDimension1D << std::endl;
            std::cout << " - maxTextureDimension2D: " << limits.limits.maxTextureDimension2D << std::endl;
            std::cout << " - maxTextureDimension3D: " << limits.limits.maxTextureDimension3D << std::endl;
            std::cout << " - maxTextureArrayLayers: " << limits.limits.maxTextureArrayLayers << std::endl;
            // { { Extra device limits } }
        }
#endif

        
    }
}

Application::Application()
    : m_IsFullyInitialized(false)
    , m_Window(nullptr)
    , m_Instance(nullptr)
    , m_Surface(nullptr)
    , m_Device(nullptr)
    , m_Queue(nullptr)
{

}

Application::~Application()
{
    Terminate();
}

bool Application::Initialize()
{
    if (!glfwInit()) 
    {
        std::cerr << "Could not initialize GLFW!" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // Create the window
    m_Window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

    if (!m_Window) 
    {
        std::cerr << "Could not open window!" << std::endl;
        glfwTerminate();
        return false;
    }

    if (!GetInstance())
    {
        std::cerr << "Getting instance failed" << std::endl;
        return false;
    }

    if (!GetAdapter())
    {
        std::cerr << "Getting adapter failed" << std::endl;
        return false;
    }

    if (!GetFeaturesAndProperties())
    {
        std::cerr << "Getting features and properties failed" << std::endl;
        return false;
    }

    if (!GetDevice())
    {
        std::cerr << "Getting device failed" << std::endl;
        return false;
    }

    if (!GetSurface())
    {
        std::cerr << "Getting surface failed" << std::endl;
        return false;
    }

    if (!GetQueue())
    {
        std::cerr << "Getting queue failed" << std::endl;
        return false;
    }

    m_IsFullyInitialized = true;
    
    return true;
}

void Application::Terminate()
{
    m_IsFullyInitialized = false;


    if (m_Queue)
    {
#ifdef __EMSCRIPTEN__
        auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, WGPUStringView , void* , void* ) { //message, void* userdata1, void* userdata2) {
            std::cout << "Queued work finished with status: " << status << std::endl;
        };

        WGPUQueueWorkDoneCallbackInfo callbackInfo2;
        callbackInfo2.nextInChain = nullptr;
        callbackInfo2.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo2.callback = onQueueWorkDone;
        callbackInfo2.userdata1 = nullptr;
        callbackInfo2.userdata2 = nullptr;

        WGPUFuture future = wgpuQueueOnSubmittedWorkDone(m_Queue, callbackInfo2);

        WGPUFutureWaitInfo waitInfo{};
        waitInfo.future = future;
        waitInfo.completed = false;

        while (wgpuInstanceWaitAny(m_Instance, 1, &waitInfo, 0) != WGPUWaitStatus::WGPUWaitStatus_Success)
        {
            std::cout << "Waiting for que to be done..." << std::endl;
            emscripten_sleep(100); // Or just return control to browser
        }

#else
        auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void*, void*) {
            std::cout << "Queued work finished with status: " << status << std::endl;
        };

        WGPUQueueWorkDoneCallbackInfo2 callbackInfo2;
        callbackInfo2.nextInChain = nullptr;
        callbackInfo2.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo2.callback = onQueueWorkDone;
        callbackInfo2.userdata1 = nullptr;
        callbackInfo2.userdata2 = nullptr;

        wgpuQueueOnSubmittedWorkDone2(m_Queue, callbackInfo2);
#endif

        wgpuQueueRelease(m_Queue);
        m_Queue = nullptr;
    }

    if (m_Surface)
    {
        wgpuSurfaceRelease(m_Surface);
        m_Surface = nullptr;
    }

    if (m_Device)
    {
        wgpuDeviceRelease(m_Device);
        m_Device = nullptr;
    }

    if (m_Adapter)
    {
        wgpuAdapterRelease(m_Adapter);
        m_Adapter = nullptr;
    }

    if (m_Instance)
    {
        wgpuInstanceRelease(m_Instance);
        m_Instance = nullptr;
    }

    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    glfwTerminate();
}

void Application::MainLoop()
{
    // Get the next target texture view
    auto [surfaceTexture, targetView] = GetNextSurfaceViewData();
    if (!targetView) return;

#ifndef WEBGPU_BACKEND_WGPU
    // We no longer need the texture, only its view
    // (NB: with wgpu-native, surface textures must be release after the call to wgpuSurfacePresent)
    wgpuTextureRelease(surfaceTexture.texture);
#endif // WEBGPU_BACKEND_WGPU

    // mouse/key event, which we don't use so far)
    glfwPollEvents();

    WGPURenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.view = targetView;
    renderPassColorAttachment.resolveTarget = nullptr;
    renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
    renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
    renderPassColorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
    renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain = nullptr;
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites = nullptr;


    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain = nullptr;
#ifdef __EMSCRIPTEN__
    //encoderDesc.label = "My command encoder";
#else
    encoderDesc.label = "My command encoder";
#endif
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_Device, &encoderDesc);

    // [...] Describe Render Pass

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    // [...] Use Render Pass
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    // Finish encoding
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuCommandEncoderRelease(encoder);

    // Submit to GPU
    wgpuQueueSubmit(m_Queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);


#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(m_Surface);
#endif

    // At the end of the frame
    wgpuTextureViewRelease(targetView);

#ifdef WEBGPU_BACKEND_WGPU
    wgpuTextureRelease(surfaceTexture.texture);
#endif
}

bool Application::IsRunning()
{
    return !glfwWindowShouldClose(m_Window);
}

std::pair<WGPUSurfaceTexture, WGPUTextureView> Application::GetNextSurfaceViewData() 
{
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(m_Surface, &surfaceTexture);

#ifdef __EMSCRIPTEN__
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal)
#else
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
#endif
    {
        return { surfaceTexture, nullptr };
    }

    WGPUTextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
#ifdef __EMSCRIPTEN__
    //viewDescriptor.label = "Surface texture view";
#else
    viewDescriptor.label = "Surface texture view";
#endif
    viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect = WGPUTextureAspect_All;
    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

    return { surfaceTexture, targetView };
}

bool Application::GetInstance()
{
    // We create a descriptor
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
    // Make sure the uncaptured error callback is called as soon as an error
    // occurs rather than at the next call to "wgpuDeviceTick".
    WGPUDawnTogglesDescriptor toggles;
    toggles.chain.next = nullptr;
    toggles.chain.sType = WGPUSType_DawnTogglesDescriptor;
    toggles.disabledToggleCount = 0;
    toggles.enabledToggleCount = 1;
    const char* toggleName = "enable_immediate_error_handling";
    toggles.enabledToggles = &toggleName;

    desc.nextInChain = &toggles.chain;
#endif // WEBGPU_BACKEND_DAWN

    // We create the instance using this descriptor
#ifdef WEBGPU_BACKEND_EMSCRIPTEN
    m_Instance = wgpuCreateInstance(nullptr);
#else //  WEBGPU_BACKEND_EMSCRIPTEN
    m_Instance = wgpuCreateInstance(&desc);
#endif //  WEBGPU_BACKEND_EMSCRIPTEN

    // We can check whether there is actually an instance created
    if (!m_Instance)
    {
        std::cerr << "Could not initialize WebGPU!" << std::endl;
        return false;
    }

    std::cout << "WGPU instance: " << m_Instance << std::endl;

    return true;
}

bool Application::GetSurface()
{
    std::cout << "Getting surface Started from window " << (m_Window ? (void*)m_Window : "nullptr") << std::endl;
#ifdef __EMSCRIPTEN__

    WGPUStringView selector{};
    selector.data = "#canvas";// ID of your HTML canvas
    selector.length = strlen(selector.data);

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector = selector; 

    WGPUSurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);

    m_Surface = wgpuInstanceCreateSurface(m_Instance, &surfaceDesc);
#else
    m_Surface = glfwGetWGPUSurface(m_Instance, m_Window);
#endif
    std::cout << "Getting surface Started " << std::endl;
    if (m_Surface == nullptr)
        return false;

    WGPUSurfaceCapabilities capabilityes;
    capabilityes.nextInChain = nullptr;
    wgpuSurfaceGetCapabilities(m_Surface, m_Adapter, &capabilityes);
       
    
    WGPUSurfaceConfiguration config = {};
    config.nextInChain = nullptr;
    config.width = 640;
    config.height = 480;
    config.format = capabilityes.formats[0];
    // And we do not need any particular view format:
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.device = m_Device;

    wgpuSurfaceConfigure(m_Surface, &config);

    return true;
}

bool Application::GetAdapter()
{
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.nextInChain = nullptr;
    m_Adapter = requestAdapterSync(m_Instance, &adapterOpts);

    std::cout << "Got adapter: " << m_Adapter << std::endl;

    // Getting the limits
    // ----------------------------------------------------------------------------------------
    // as of April 1st, 2024, wgpuAdapterGetLimits is not implemented yet on Google Chrome, hence the #ifndef

#ifndef __EMSCRIPTEN__
    WGPUSupportedLimits supportedLimits = {};
    supportedLimits.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
    const bool get_limit_success = wgpuAdapterGetLimits(m_Adapter, &supportedLimits) == WGPUStatus_Success;
#else
    const bool get_limit_success = wgpuAdapterGetLimits(m_Adapter, &supportedLimits);
#endif

    if (get_limit_success)
    {
        std::cout << "Adapter limits:" << std::endl;
        std::cout << " - maxTextureDimension1D: " << supportedLimits.limits.maxTextureDimension1D << std::endl;
        std::cout << " - maxTextureDimension2D: " << supportedLimits.limits.maxTextureDimension2D << std::endl;
        std::cout << " - maxTextureDimension3D: " << supportedLimits.limits.maxTextureDimension3D << std::endl;
        std::cout << " - maxTextureArrayLayers: " << supportedLimits.limits.maxTextureArrayLayers << std::endl;
    }
    return get_limit_success;
#else
    WGPULimits supportedLimits = {};
    supportedLimits.nextInChain = nullptr;

    supportedLimits.minStorageBufferOffsetAlignment = 256;
    supportedLimits.minUniformBufferOffsetAlignment = 256;

    wgpuAdapterGetLimits(m_Adapter, &supportedLimits);

    std::cout << "Adapter limits:" << std::endl;
    std::cout << " - maxTextureDimension1D: " << supportedLimits.maxTextureDimension1D << std::endl;
    std::cout << " - maxTextureDimension2D: " << supportedLimits.maxTextureDimension2D << std::endl;
    std::cout << " - maxTextureDimension3D: " << supportedLimits.maxTextureDimension3D << std::endl;
    std::cout << " - maxTextureArrayLayers: " << supportedLimits.maxTextureArrayLayers << std::endl;

    return true;

#endif 
}

bool Application::GetFeaturesAndProperties()
{
#ifndef __EMSCRIPTEN__
    // Getting the features
    // ----------------------------------------------------------------------------------------
    std::vector<WGPUFeatureName> features;

    // Call the function a first time with a null return address, just to get
    // the entry count.
    size_t featureCount = wgpuAdapterEnumerateFeatures(m_Adapter, nullptr);

    // Allocate memory (could be a new, or a malloc() if this were a C program)
    features.resize(featureCount);

    // Call the function a second time, with a non-null return address
    wgpuAdapterEnumerateFeatures(m_Adapter, features.data());

    std::cout << "Adapter features:" << std::endl;
    std::cout << std::hex; // Write integers as hexadecimal to ease comparison with webgpu.h literals
    for (auto f : features) 
    {
        std::cout << " - 0x" << f << std::endl;
    }
    std::cout << std::dec; // Restore decimal numbers

    // Getting properties
    // ----------------------------------------------------------------------------------------

    WGPUAdapterProperties properties = {};
    properties.nextInChain = nullptr;
    wgpuAdapterGetProperties(m_Adapter, &properties);
    std::cout << "Adapter properties:" << std::endl;
    std::cout << " - vendorID: " << properties.vendorID << std::endl;
    if (properties.vendorName) 
        std::cout << " - vendorName: " << properties.vendorName << std::endl;
    
    if (properties.architecture)
        std::cout << " - architecture: " << properties.architecture << std::endl;

    std::cout << " - deviceID: " << properties.deviceID << std::endl;
    if (properties.name)
        std::cout << " - name: " << properties.name << std::endl;
    
    if (properties.driverDescription) 
        std::cout << " - driverDescription: " << properties.driverDescription << std::endl;

    std::cout << std::hex;
    std::cout << " - adapterType: 0x" << properties.adapterType << std::endl;
    std::cout << " - backendType: 0x" << properties.backendType << std::endl;
    std::cout << std::dec; // Restore decimal numbers
#endif

    return true;
}

bool Application::GetDevice()
{
    // Creating device
    // ----------------------------------------------------------------------------------------

    std::cout << "Requesting device..." << std::endl;

    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.nextInChain = nullptr;
#ifdef __EMSCRIPTEN__
    //deviceDesc.label = "My Device"; // anything works here, that's your call

    auto callback = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
        std::cout << "Uncaptured device error: type " << type;
        std::cout << " (" << (message.length > 0 ? message.data : "") << ")";
        std::cout << std::endl;
    };

    WGPUUncapturedErrorCallbackInfo uncapturedErrorCallbackInfo{};
    uncapturedErrorCallbackInfo.nextInChain = nullptr;
    uncapturedErrorCallbackInfo.callback = callback;
    uncapturedErrorCallbackInfo.userdata1 = nullptr;
    uncapturedErrorCallbackInfo.userdata2 = nullptr;

    deviceDesc.uncapturedErrorCallbackInfo = uncapturedErrorCallbackInfo;

#else
    deviceDesc.label = "My Device"; // anything works here, that's your call
#endif
    deviceDesc.requiredFeatureCount = 0; // we do not require any specific feature
    deviceDesc.requiredLimits = nullptr; // we do not require any specific limit
    deviceDesc.defaultQueue.nextInChain = nullptr;
#ifdef __EMSCRIPTEN__
    //WGPUStringView label;
    //label.
    //deviceDesc.defaultQueue.label = "The default queue";
#else
    deviceDesc.defaultQueue.label = "The default queue";
#endif
  
#ifdef __EMSCRIPTEN__

    auto deviceLostCallback = [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void* userdata1, void*) {
        std::cout << "Device lost: reason " << reason << ", UserData : " << (userdata1 ? userdata1 : "nullptr") << std::endl;
        std::cout << " (" << (message.length > 0 ? message.data : "") << ")";
        std::cout << std::endl;
    };

    WGPUDeviceLostCallbackInfo deviceLostCallbackInfo{};
    deviceLostCallbackInfo.nextInChain = nullptr;
    deviceLostCallbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
    deviceLostCallbackInfo.callback = deviceLostCallback;
    deviceLostCallbackInfo.userdata1 = nullptr;
    deviceLostCallbackInfo.userdata2 = nullptr;

    deviceDesc.deviceLostCallbackInfo = deviceLostCallbackInfo;

#else
    deviceDesc.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason, char const* message, void* userdata) {
        std::cout << "Device lost: reason " << reason << ", UserData : " << userdata << std::endl;
        std::cout << " (" << (message ? message : "") << ")";
        std::cout << std::endl;
    };
    
#endif

    m_Device = requestDeviceSync(m_Instance, m_Adapter, &deviceDesc);

    if (m_Device == nullptr)
        return false;

    std::cout << "Got device: " << m_Device << std::endl;

    inspectDevice(m_Device);

#ifndef __EMSCRIPTEN__

    auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */) {
        std::cout << "Uncaptured device error: type " << type;
        std::cout << " (" << (message ? message : "") << ")";
        std::cout << std::endl;
    };

    wgpuDeviceSetUncapturedErrorCallback(m_Device, onDeviceError, nullptr /* pUserData */);
#endif
    return true;
}

bool Application::GetQueue()
{
    m_Queue = wgpuDeviceGetQueue(m_Device);

    if (m_Queue == nullptr)
        return false;
  

    //wgpuCommandEncoderInsertDebugMarker(m_Encoder, "Do one thing");
    //wgpuCommandEncoderInsertDebugMarker(m_Encoder, "Do another thing");

    

    /*WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.nextInChain = nullptr;
    cmdBufferDescriptor.label = "Command buffer";
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(m_Encoder, &cmdBufferDescriptor);
    wgpuCommandEncoderRelease(m_Encoder); // release encoder after it's finished*/

    // Finally submit the command queue
    /*std::cout << "Submitting command..." << std::endl;
    wgpuQueueSubmit(queue, 1, &command);
    wgpuCommandBufferRelease(command);
    std::cout << "Command submitted." << std::endl;

    for (int i = 0; i < 5; ++i) 
    {
        std::cout << "Tick/Poll device..." << std::endl;
#if defined(WEBGPU_BACKEND_DAWN)
        wgpuDeviceTick(m_Device);
#elif defined(WEBGPU_BACKEND_WGPU)
        wgpuDevicePoll(m_Device, false, nullptr);
#elif defined(WEBGPU_BACKEND_EMSCRIPTEN)
        emscripten_sleep(100);
#endif
    }*/

    return true;
}