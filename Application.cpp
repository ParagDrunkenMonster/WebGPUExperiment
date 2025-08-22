#include "Application.h"

#include <iostream>


#include <cassert>
#include <vector>
#include <array>
#include <PANDUMatrix44.h>
#include <PANDUVector4.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <thread>
#endif // __EMSCRIPTEN__

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

const char* shaderSource = R"(

    struct ConstantUniforms {
        projectionMatrix    : mat4x4<f32>,
        viewMatrix          : mat4x4<f32>,
        invProjectionMatrix : mat4x4<f32>,
        invViewMatrix       : mat4x4<f32>,
        totalTime : f32,
        deltaTime : f32,
        // pad to 16B if needed
    };

    struct DynamicUniforms {
        modelMatrix    : mat4x4<f32>,
        invModelMatrix : mat4x4<f32>,
        color          : vec4<f32>,
    };

    @group(0) @binding(0) var<uniform> constUniforms : ConstantUniforms;
    @group(0) @binding(1) var<uniform> dynUniforms   : DynamicUniforms;


    struct VertexInput {
        @location(0) position: vec3f,
        @location(1) color: vec3f,
    };

    struct VertexOutput {
        @builtin(position) position: vec4f,
        @location(0) color: vec3f,
    };

    @vertex
    fn vs_main(in: VertexInput) -> VertexOutput {
        
        var out: VertexOutput; // create the output struct
        
        let ratio = 640.0 / 480.0;

        out.position = dynUniforms.modelMatrix * vec4f(in.position, 1.0);
        let color = in.color * dynUniforms.color.rgb;

        // Gamma-correction
        out.color = pow(color, vec3f(2.2));

        return out;
    }

    @fragment
    fn fs_main(in: VertexOutput) -> @location(0) vec4f {
        return vec4f(in.color, 1.0); // use the interpolated color coming from the vertex shader
    }
)";

struct alignas(16) ConstantUniforms
{
    std::array<float, 16> projectionMatrix;
    std::array<float, 16> viewMatrix;
    std::array<float, 16> invProjectionMatrix;
    std::array<float, 16> invViewMatrix;
    float totalTime;
    float pad[3];
    float deltaTime;
    float pad2[3];
};

struct alignas(16) DynamicUniforms
{
    std::array<float, 16> modelMatrix;
    std::array<float, 16> invModelMatrix;
    std::array<float, 4> color;
};



std::vector<float> vertexData = {
    // x,       y,      z       r,   g,   b
    // The base
    - 0.5f, - 0.5f, - 0.3f,    1.0f, 1.0f, 1.0f,
    + 0.5f, - 0.5f, - 0.3f,    1.0f, 1.0f, 1.0f,
    + 0.5f, + 0.5f, - 0.3f,    1.0f, 1.0f, 1.0f,
    - 0.5f, + 0.5f, - 0.3f,    1.0f, 1.0f, 1.0f,

    // And the tip of the pyramid
    + 0.0f, + 0.0f, + 0.5f,    0.5f, 0.5f, 0.5f
};

std::vector<uint16_t> indexData = {
    0,  1,  2,
    0,  2,  3,
    0,  1,  4,
    1,  2,  4,
    2,  3,  4,
    3,  0,  4,
};

const uint32_t vertexCount = static_cast<uint32_t>(vertexData.size() / 6);

const uint32_t maxDrawCallsPerFrameSupported = 1024;

namespace
{
    void FillConstantUniform(ConstantUniforms& OutUniform, const Pandu::Matrix44& Projection, const Pandu::Matrix44& View, float TotalTime, float DeltaTime)
    {
        Pandu::Matrix44 InvProjection = Projection.GetInverse(), InvView = View.GetInverse();

        std::copy(&Projection.m[0][0], (&Projection.m[0][0]) + 16, OutUniform.projectionMatrix.begin());
        std::copy(&View.m[0][0], (&View.m[0][0]) + 16, OutUniform.viewMatrix.begin());

        std::copy(&InvProjection.m[0][0], (&InvProjection.m[0][0]) + 16, OutUniform.invProjectionMatrix.begin());
        std::copy(&InvView.m[0][0], (&InvView.m[0][0]) + 16, OutUniform.invViewMatrix.begin());

        OutUniform.totalTime = TotalTime;
        OutUniform.deltaTime = DeltaTime;
    }

    void FillDynamicUniform(DynamicUniforms& OutUniform, const Pandu::Matrix44& Model, const Pandu::Vector4& Color)
    {
        Pandu::Matrix44 InvModel = Model.GetInverse();

        std::copy(&Model.m[0][0], (&Model.m[0][0]) + 16, OutUniform.modelMatrix.begin());
        std::copy(&InvModel.m[0][0], (&InvModel.m[0][0]) + 16, OutUniform.invModelMatrix.begin());

        std::copy(&Color.Data()[0], (&Color.Data()[0]) + 4, OutUniform.color.begin());
    }

    uint32_t ceilToNextMultiple(uint32_t value, uint32_t step) 
    {
        const uint32_t divide_and_ceil = value / step + (value % step == 0 ? 0 : 1);
        return step * divide_and_ceil;
    }

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
#ifndef __EMSCRIPTEN__
    void inspectDevice(WGPUDevice device) 
#else
    void inspectDevice(WGPUDevice)
#endif
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
    }
}

Application::Application()
    : m_IsFullyInitialized(false)
    , m_Window(nullptr)
    , m_Instance(nullptr)
    , m_Surface(nullptr)
    , m_Device(nullptr)
    , m_Queue(nullptr)
    , m_BindGroupLayout(nullptr)
    , m_PipelineLayout(nullptr)
    , m_BindGroup(nullptr)
    , m_Pipeline(nullptr)
    , m_ShaderModule(nullptr)
    , m_VertexBufferSize(0)
    , m_Buffer1(nullptr)
    , m_IndexBufferSize(0)
    , m_IndexBuffer(nullptr)
    , m_ConstantUniformBufferSize(0)
    , m_ConstantUniformBufferStride(0)
    , m_DynamicsUniformBufferSize(0)
    , m_DynamicsUniformBufferStride(0)
    , m_UniformBuffer(nullptr)
    , m_DepthTexture(nullptr)
    , m_DepthTextureView(nullptr)
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

    if (!LoadShaders())
    {
        std::cerr << "Shader module load failed" << std::endl;
        return false;
    }

    if (!CreatePipeline())
    {
        std::cerr << "Create pipeline failed" << std::endl;
        return false;
    }

    if (!CreateUniformBuffer())
    {
        std::cerr << "Create uniform buffer failed" << std::endl;
        return false;
    }

    if (!CreateVertexBuffer())
    {
        std::cerr << "Creating vertex buffer failed" << std::endl;
        return false;
    }

    if (!CreateIndexBuffer())
    {
        std::cerr << "Creating index buffer failed" << std::endl;
        return false;
    }

    //{ { Write input data } }

    //{ { Encode and submit the buffer to buffer copy } }

    //{ { Read buffer data back } }

    //{ { Release buffers } }


    m_IsFullyInitialized = true;
    
    return true;
}

void Application::Terminate()
{
    m_IsFullyInitialized = false;

    DestroyBuffer(m_Buffer1);
    DestroyBuffer(m_IndexBuffer);
    DestroyBuffer(m_UniformBuffer);

    if (m_ShaderModule)
    {
        wgpuShaderModuleRelease(m_ShaderModule);
        m_ShaderModule = nullptr;
    }

    if (m_PipelineLayout)
    {
        wgpuPipelineLayoutRelease(m_PipelineLayout);
    }
    if (m_BindGroupLayout)
    {
        wgpuBindGroupLayoutRelease(m_BindGroupLayout);
        m_BindGroupLayout = nullptr;
    }

    if (m_BindGroup)
    {
        wgpuBindGroupRelease(m_BindGroup);
        m_BindGroup = nullptr;
    }

    if (m_Pipeline)
    {
        wgpuRenderPipelineRelease(m_Pipeline);
        m_Pipeline = nullptr;
    }

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

    if (m_DepthTextureView)
    {
        wgpuTextureViewRelease(m_DepthTextureView);
        m_DepthTextureView = nullptr;
    }

    if (m_DepthTexture)
    {
        wgpuTextureDestroy(m_DepthTexture);
        wgpuTextureRelease(m_DepthTexture);
        m_DepthTexture = nullptr;
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

void Application::DestroyBuffer(WGPUBuffer& Buffer)
{
    if (Buffer)
    {
        wgpuBufferRelease(Buffer);
        //wgpuBufferDestroy(Buffer); // To force destroy the buffer
        Buffer = nullptr;
    }
}

void Application::MainLoop()
{
    std::pair<WGPUSurfaceTexture, WGPUTextureView> SurfaceViewData;
    GetNextSurfaceViewData(SurfaceViewData);
    auto [surfaceTexture, targetView] = SurfaceViewData;

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
    renderPassColorAttachment.clearValue = WGPUColor{ 0.2, 0.2, 0.2, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
    renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

    // We now add a depth/stencil attachment:
    WGPURenderPassDepthStencilAttachment depthStencilAttachment;
    // The view of the depth texture
    depthStencilAttachment.view = m_DepthTextureView;

    // The initial value of the depth buffer, meaning "far"
    depthStencilAttachment.depthClearValue = 1.0f;
    // Operation settings comparable to the color attachment
    depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
    // we could turn off writing to the depth buffer globally here
    depthStencilAttachment.depthReadOnly = false;

    // Stencil setup, mandatory but unused
    depthStencilAttachment.stencilClearValue = 0;
    depthStencilAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthStencilAttachment.stencilReadOnly = true;
    
    
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain = nullptr;
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = &depthStencilAttachment;
    renderPassDesc.timestampWrites = nullptr;


    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain = nullptr;

    SET_WGPU_LABEL(encoderDesc, "My command encoder");

    const float Time = (float)glfwGetTime();
    static float TotalTime = 0;

    ConstantUniforms ConstData;
    FillConstantUniform(ConstData, Pandu::Matrix44::IDENTITY, Pandu::Matrix44::IDENTITY, TotalTime, Time);
    wgpuQueueWriteBuffer(m_Queue, m_UniformBuffer, 0, &ConstData, sizeof(ConstantUniforms));

    int ObjIndex = 0;
    Pandu::Matrix44 Translate0 = Pandu::Matrix44::IDENTITY;
    Translate0.SetTranslate(Pandu::Vector3(-0.5f, -0.5f, -0.25f));
    DynamicUniforms DynData;
    FillDynamicUniform(DynData, Translate0, Pandu::Vector4::UNIT);
    wgpuQueueWriteBuffer(m_Queue, m_UniformBuffer, m_ConstantUniformBufferStride + m_DynamicsUniformBufferStride * ObjIndex, &DynData, sizeof(DynamicUniforms));

    // for second object
    Pandu::Matrix44 Translate1 = Pandu::Matrix44::IDENTITY;
    Translate1.SetTranslate(Pandu::Vector3(0.5f, 0.5f, -0.25f));
    ObjIndex++;
    DynamicUniforms DynData1;
    FillDynamicUniform(DynData1, Translate1, Pandu::Vector4::UNIT);
    wgpuQueueWriteBuffer(m_Queue, m_UniformBuffer, m_ConstantUniformBufferStride + m_DynamicsUniformBufferStride * ObjIndex, &DynData1, sizeof(DynamicUniforms));


    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_Device, &encoderDesc);

    // [...] Describe Render Pass

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Select which render pipeline to use
    wgpuRenderPassEncoderSetPipeline(renderPass, m_Pipeline);

    // Set vertex buffer while encoding the render pass
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, m_Buffer1, 0, m_VertexBufferSize);

    wgpuRenderPassEncoderSetIndexBuffer(renderPass, m_IndexBuffer, WGPUIndexFormat_Uint16, 0, m_IndexBufferSize);

    const uint32_t IndicesCount = (uint32_t)indexData.size();

    // Set binding group
    uint32_t dynamicOffset = 0 * m_DynamicsUniformBufferStride;
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_BindGroup, 1, &dynamicOffset);
    wgpuRenderPassEncoderDrawIndexed(renderPass, IndicesCount, 1, 0, 0, 0);

    dynamicOffset = 1 * m_DynamicsUniformBufferStride;
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_BindGroup, 1, &dynamicOffset);
    wgpuRenderPassEncoderDrawIndexed(renderPass, IndicesCount, 1, 0, 0, 0);
 
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

void Application::GetNextSurfaceViewData(std::pair<WGPUSurfaceTexture, WGPUTextureView>& SurfaceViewData)
{
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(m_Surface, &surfaceTexture);

#ifdef __EMSCRIPTEN__
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal)
#else
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
#endif
    {
        SurfaceViewData ={ surfaceTexture, nullptr };
        return;
    }

    WGPUTextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;

    SET_WGPU_LABEL(viewDescriptor, "Surface texture view");

    viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect = WGPUTextureAspect_All;
    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

    SurfaceViewData ={ surfaceTexture, targetView };
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

    WGPUSurfaceCapabilities capabilities;
    capabilities.nextInChain = nullptr;
    wgpuSurfaceGetCapabilities(m_Surface, m_Adapter, &capabilities);

    m_SurfaceFormat = capabilities.formats[0];
       
    
    WGPUSurfaceConfiguration config = {};
    config.nextInChain = nullptr;
    config.width = 640;
    config.height = 480;
    config.format = capabilities.formats[0];
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

    // Start get adapter limits
    // -------------------------------------------------------------------------------------------------
#ifndef __EMSCRIPTEN__
    WGPUSupportedLimits supportedLimits = {};
    supportedLimits.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
    const bool get_limit_success = wgpuAdapterGetLimits(m_Adapter, &supportedLimits) & WGPUStatus_Success;
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

        std::cout << "adapter.maxVertexAttributes: " << supportedLimits.limits.maxVertexAttributes << std::endl;

        m_AdopterLimits = supportedLimits.limits;
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

    std::cout << "adapter.maxVertexAttributes: " << supportedLimits.maxVertexAttributes << std::endl;

    m_AdopterLimits = supportedLimits;

    return true;

#endif 
    // End get adapter limits
    // -------------------------------------------------------------------------------------------------
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

    WGPUQueueDescriptor& defaultQueue = deviceDesc.defaultQueue;

    SET_WGPU_LABEL(deviceDesc, "My Device");
    SET_WGPU_LABEL(defaultQueue, "he Default Queue");

#ifdef __EMSCRIPTEN__
    WGPULimits requiredLimits{};
    requiredLimits = m_AdopterLimits;

    /*// We use at most 1 vertex attribute for now
    requiredLimits.maxVertexAttributes = 1;
    // We should also tell that we use 1 vertex buffers
    requiredLimits.maxVertexBuffers = 1;
    // Maximum size of a buffer is 6 vertices of 2 float each
    requiredLimits.maxBufferSize = 6 * 2 * sizeof(float);
    // Maximum stride between 2 consecutive vertices in the vertex buffer
    requiredLimits.maxVertexBufferArrayStride = 2 * sizeof(float);

    requiredLimits.maxInterStageShaderComponents = 3;
    */

    deviceDesc.requiredLimits = &requiredLimits; // we do not require any specific limit

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

    WGPURequiredLimits requiredLimits{};
    requiredLimits.limits = m_AdopterLimits;

    // We use at most 1 vertex attribute for now
    /*requiredLimits.limits.maxVertexAttributes = 1;
    // We should also tell that we use 1 vertex buffers
    requiredLimits.limits.maxVertexBuffers = 1;
    // Maximum size of a buffer is 6 vertices of 2 float each
    requiredLimits.limits.maxBufferSize = 6 * 2 * sizeof(float);
    // Maximum stride between 2 consecutive vertices in the vertex buffer
    requiredLimits.limits.maxVertexBufferArrayStride = 2 * sizeof(float);*/

    deviceDesc.requiredLimits = &requiredLimits; // we do not require any specific limit

    deviceDesc.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason, char const* message, void* userdata) {
        std::cout << "Device lost: reason " << reason << ", UserData : " << userdata << std::endl;
        std::cout << " (" << (message ? message : "") << ")";
        std::cout << std::endl;
    };
#endif

    deviceDesc.requiredFeatureCount = 0; // we do not require any specific feature
    deviceDesc.defaultQueue.nextInChain = nullptr;

    m_Device = requestDeviceSync(m_Instance, m_Adapter, &deviceDesc);

    if (m_Device == nullptr)
        return false;

    std::cout << "Got device: " << m_Device << std::endl;

    inspectDevice(m_Device);

    // Start get device limits
    // -------------------------------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
    WGPULimits limits;
    limits.nextInChain = nullptr;

    limits.minStorageBufferOffsetAlignment = 256;
    limits.minUniformBufferOffsetAlignment = 256;

    const bool success = wgpuDeviceGetLimits(m_Device, &limits) & WGPUStatus_Success;

    if (success)
    {
        std::cout << "Device limits:" << std::endl;
        std::cout << " - maxTextureDimension1D: " << limits.maxTextureDimension1D << std::endl;
        std::cout << " - maxTextureDimension2D: " << limits.maxTextureDimension2D << std::endl;
        std::cout << " - maxTextureDimension3D: " << limits.maxTextureDimension3D << std::endl;
        std::cout << " - maxTextureArrayLayers: " << limits.maxTextureArrayLayers << std::endl;

        std::cout << "device.maxVertexAttributes: " << limits.maxVertexAttributes << std::endl;
        // { { Extra device limits } }

        m_DeviceLimits = limits;
    }
#else
    WGPUSupportedLimits limits = {};
    limits.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
    const bool success = wgpuDeviceGetLimits(m_Device, &limits) & WGPUStatus_Success;
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

        std::cout << "device.maxVertexAttributes: " << limits.limits.maxVertexAttributes << std::endl;

        // { { Extra device limits } }

        m_DeviceLimits = limits.limits;
    }
#endif
    // End get device limits
    // -------------------------------------------------------------------------------------------------

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

bool Application::LoadShaders()
{
#ifdef __EMSCRIPTEN__

    WGPUShaderSourceWGSL shaderCodeDesc{};
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = WGPUSType_ShaderSourceWGSL;

    WGPUStringView code{};
    code.data = shaderSource;// ID of your HTML canvas
    code.length = strlen(shaderSource);

    shaderCodeDesc.code = code;

    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&shaderCodeDesc);;
#else
    WGPUShaderModuleWGSLDescriptor shaderCodeDesc{};
    // Set the chained struct's header
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    shaderCodeDesc.code = shaderSource;

    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&shaderCodeDesc);;
#endif

    SET_WGPU_LABEL(shaderDesc, "Basic Shader");

   
#ifdef WEBGPU_BACKEND_WGPU
    shaderDesc.hintCount = 0;
    shaderDesc.hints = nullptr;
#endif

    // [...] Describe shader module
    m_ShaderModule = wgpuDeviceCreateShaderModule(m_Device, &shaderDesc);

    return m_ShaderModule != nullptr;
}

void setDefaultDepthStencilFace(WGPUStencilFaceState& stencilFaceState)
{
    stencilFaceState.compare = WGPUCompareFunction_Always;
    stencilFaceState.failOp = WGPUStencilOperation_Keep;
    stencilFaceState.depthFailOp = WGPUStencilOperation_Keep;
    stencilFaceState.passOp = WGPUStencilOperation_Keep;
}

bool Application::CreatePipeline()
{
    //setup depth stencil
    WGPUDepthStencilState depthStencilState;
    depthStencilState.nextInChain = nullptr;
    depthStencilState.stencilReadMask = 0;
    depthStencilState.stencilWriteMask = 0;
    depthStencilState.depthBias = 0;
    depthStencilState.depthBiasSlopeScale = 0;
    depthStencilState.depthBiasClamp = 0;
    depthStencilState.depthCompare = WGPUCompareFunction_Less;
#ifdef __EMSCRIPTEN__
    depthStencilState.depthWriteEnabled = WGPUOptionalBool_True;
#else
    depthStencilState.depthWriteEnabled = true;
#endif
    depthStencilState.format = WGPUTextureFormat_Depth24Plus;
    setDefaultDepthStencilFace(depthStencilState.stencilFront);
    setDefaultDepthStencilFace(depthStencilState.stencilBack);

    // Create the depth texture
    WGPUTextureDescriptor depthTextureDesc;
    depthTextureDesc.nextInChain = nullptr;

    SET_WGPU_LABEL(depthTextureDesc, "Depth Texture");

    depthTextureDesc.dimension = WGPUTextureDimension_2D;
    depthTextureDesc.format = depthStencilState.format;
    depthTextureDesc.mipLevelCount = 1;
    depthTextureDesc.sampleCount = 1;
    depthTextureDesc.size = { 640, 480, 1 };
    depthTextureDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthTextureDesc.viewFormatCount = 1;
    depthTextureDesc.viewFormats = &depthStencilState.format;
    m_DepthTexture = wgpuDeviceCreateTexture(m_Device, &depthTextureDesc);

    // Create the view of the depth texture manipulated by the rasterizer
    WGPUTextureViewDescriptor depthTextureViewDesc;
    depthTextureViewDesc.nextInChain = nullptr;

    SET_WGPU_LABEL(depthTextureViewDesc, "Depth texture view");

    depthTextureViewDesc.aspect = WGPUTextureAspect_DepthOnly;
    depthTextureViewDesc.baseArrayLayer = 0;
    depthTextureViewDesc.arrayLayerCount = 1;
    depthTextureViewDesc.baseMipLevel = 0;
    depthTextureViewDesc.mipLevelCount = 1;
    depthTextureViewDesc.dimension = WGPUTextureViewDimension_2D;
    depthTextureViewDesc.format = depthStencilState.format;
    m_DepthTextureView = wgpuTextureCreateView(m_DepthTexture, &depthTextureViewDesc);


    m_ConstantUniformBufferStride = ceilToNextMultiple((uint32_t)sizeof(ConstantUniforms), (uint32_t)m_DeviceLimits.minUniformBufferOffsetAlignment);
    m_ConstantUniformBufferSize = m_ConstantUniformBufferStride;

    m_DynamicsUniformBufferStride = ceilToNextMultiple(sizeof(DynamicUniforms), m_DeviceLimits.minUniformBufferOffsetAlignment);
    m_DynamicsUniformBufferSize = m_DynamicsUniformBufferStride * maxDrawCallsPerFrameSupported;

    WGPUBindGroupLayoutEntry BindingLayout[2] = {};

    // --- Per frame constant buffer (binding = 0) ---
    BindingLayout[0].binding = 0;
    BindingLayout[0].visibility = WGPUShaderStage_Vertex;
    BindingLayout[0].buffer.type = WGPUBufferBindingType_Uniform;
    BindingLayout[0].buffer.hasDynamicOffset = false;
    BindingLayout[0].buffer.minBindingSize = sizeof(ConstantUniforms);

    // --- Per draw call dynamic buffer (binding = 1) ---
    BindingLayout[1].binding = 1;
    BindingLayout[1].visibility = WGPUShaderStage_Vertex;
    BindingLayout[1].buffer.type = WGPUBufferBindingType_Uniform;
    BindingLayout[1].buffer.hasDynamicOffset = true;
    BindingLayout[1].buffer.minBindingSize = sizeof(DynamicUniforms);

        // Binding layout
    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
    bindGroupLayoutDesc.nextInChain = nullptr;

    SET_WGPU_LABEL(bindGroupLayoutDesc, "Bind Layout");

    bindGroupLayoutDesc.entryCount = 2;
    bindGroupLayoutDesc.entries = BindingLayout;

    m_BindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_Device, &bindGroupLayoutDesc);

    // Create the pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc{};
    layoutDesc.nextInChain = nullptr;

    SET_WGPU_LABEL(layoutDesc, "Layout Desc");

    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &m_BindGroupLayout;
    m_PipelineLayout = wgpuDeviceCreatePipelineLayout(m_Device, &layoutDesc);


    // Vertex fetch
    std::vector<WGPUVertexAttribute> vertexAttribs(2);
    vertexAttribs[0].format = WGPUVertexFormat::WGPUVertexFormat_Float32x3;;
    vertexAttribs[0].offset = 0;
    vertexAttribs[0].shaderLocation = 0;

    vertexAttribs[1].format = WGPUVertexFormat::WGPUVertexFormat_Float32x3;;
    vertexAttribs[1].offset = 3 * sizeof(float); // starts after x, y, z position
    vertexAttribs[1].shaderLocation = 1;

    const uint32_t StrideSize = 6 * sizeof(float);
    m_VertexBufferLayout.arrayStride = StrideSize;
    m_VertexBufferLayout.stepMode = WGPUVertexStepMode::WGPUVertexStepMode_Vertex;
    m_VertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
    m_VertexBufferLayout.attributes = vertexAttribs.data();

    WGPUMultisampleState multisample{};
    multisample.nextInChain = nullptr;
    multisample.count = 1;
    multisample.mask = ~0u;// Default value for the mask, meaning "all bits on"
    multisample.alphaToCoverageEnabled = false;

    WGPUPrimitiveState primitive{};
    primitive.nextInChain = nullptr;
    primitive.topology = WGPUPrimitiveTopology::WGPUPrimitiveTopology_TriangleList;
    primitive.stripIndexFormat = WGPUIndexFormat::WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace::WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;

    WGPUVertexState vertex{};
    vertex.nextInChain = nullptr;
    vertex.module = m_ShaderModule;
#ifdef __EMSCRIPTEN__
    WGPUStringView entryvs{};
    entryvs.data = "vs_main";
    entryvs.length = strlen(entryvs.data);

    vertex.entryPoint = entryvs;
#else
    vertex.entryPoint = "vs_main";
#endif
    vertex.constantCount = 0;
    vertex.constants = nullptr;
    vertex.bufferCount = 1;
    vertex.buffers = &m_VertexBufferLayout;


    WGPUBlendComponent blendColor{};
    blendColor.operation = WGPUBlendOperation_Add;
    blendColor.srcFactor = WGPUBlendFactor_One;
    blendColor.dstFactor = WGPUBlendFactor_Zero;

    WGPUBlendComponent blendAlpha{};
    blendAlpha.operation = WGPUBlendOperation_Add;
    blendAlpha.srcFactor = WGPUBlendFactor_One;
    blendAlpha.dstFactor = WGPUBlendFactor_Zero;

    WGPUBlendState blendState{};
    blendState.color = blendColor;
    blendState.alpha = blendAlpha;

    WGPUColorTargetState colorTarget{};
    colorTarget.nextInChain = nullptr;
    colorTarget.format = m_SurfaceFormat;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment{};
    fragment.nextInChain = nullptr;
    fragment.module = m_ShaderModule;
#ifdef __EMSCRIPTEN__
    WGPUStringView entryfs{};
    entryfs.data = "fs_main";
    entryfs.length = strlen(entryfs.data);

    fragment.entryPoint = entryfs;
#else
    fragment.entryPoint = "fs_main";
#endif
    fragment.constantCount = 0;
    fragment.constants = nullptr;
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.nextInChain = nullptr;

    SET_WGPU_LABEL(pipelineDesc, "Main Pipeline");

    pipelineDesc.layout = m_PipelineLayout;
    pipelineDesc.vertex = vertex;
    pipelineDesc.primitive = primitive;
    pipelineDesc.depthStencil = &depthStencilState;
    pipelineDesc.multisample = multisample;
    pipelineDesc.fragment = &fragment;

    m_Pipeline = wgpuDeviceCreateRenderPipeline(m_Device, &pipelineDesc);


    return m_Pipeline != nullptr;
}

bool Application::CreateVertexBuffer()
{
    m_VertexBufferSize = (uint32_t)vertexData.size() * sizeof(float);
    //BufferSize = (BufferSize + 3) & ~3; //No need since buffer is already align by 4 bytes

    WGPUBufferDescriptor vertexBufferDesc{};
    vertexBufferDesc.nextInChain = nullptr;
    vertexBufferDesc.size = m_VertexBufferSize;
    vertexBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex; // GPU-only
    vertexBufferDesc.mappedAtCreation = false;

    m_Buffer1 = wgpuDeviceCreateBuffer(m_Device, &vertexBufferDesc);

    WGPUBufferDescriptor stagingDesc{};
    stagingDesc.nextInChain = nullptr;
    stagingDesc.size = m_VertexBufferSize;
    stagingDesc.usage = WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc; // CPU-writable + copy source
    stagingDesc.mappedAtCreation = true;

    WGPUBuffer stagingBuffer = wgpuDeviceCreateBuffer(m_Device, &stagingDesc);

    void* mapped = wgpuBufferGetMappedRange(stagingBuffer, 0, stagingDesc.size);
    memcpy(mapped, vertexData.data(), stagingDesc.size);
    wgpuBufferUnmap(stagingBuffer);

    WGPUCommandEncoderDescriptor encoderDesc{};
    encoderDesc.nextInChain = nullptr;

    SET_WGPU_LABEL(encoderDesc, "Upload Encoder");

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_Device, &encoderDesc);

    wgpuCommandEncoderCopyBufferToBuffer(encoder, stagingBuffer, 0, m_Buffer1, 0, stagingDesc.size);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(m_Queue, 1, &cmd);

    //Staging buffer is not required anymore
    wgpuBufferRelease(stagingBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuCommandBufferRelease(cmd);


    // Upload geometry data to the buffer
    //wgpuQueueWriteBuffer(m_Queue, m_Buffer1, 0, vertexData.data(), bufferDesc.size);

    return true;
}

bool Application::CreateIndexBuffer()
{
    m_IndexBufferSize = (uint32_t)indexData.size() * sizeof(uint16_t);
    m_IndexBufferSize = (m_IndexBufferSize + 3) & ~3; //No need since buffer is already align by 4 bytes, since uint32_t is 4 bytes, if we use uint16_t or lower then we might have to uncomment this

    WGPUBufferDescriptor vertexBufferDesc{};
    vertexBufferDesc.nextInChain = nullptr;
    vertexBufferDesc.size = m_IndexBufferSize;
    vertexBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index; // GPU-only
    vertexBufferDesc.mappedAtCreation = false;

    m_IndexBuffer = wgpuDeviceCreateBuffer(m_Device, &vertexBufferDesc);

    WGPUBufferDescriptor stagingDesc{};
    stagingDesc.nextInChain = nullptr;
    stagingDesc.size = m_IndexBufferSize;
    stagingDesc.usage = WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc; // CPU-writable + copy source
    stagingDesc.mappedAtCreation = true;

    WGPUBuffer stagingBuffer = wgpuDeviceCreateBuffer(m_Device, &stagingDesc);

    void* mapped = wgpuBufferGetMappedRange(stagingBuffer, 0, stagingDesc.size);
    memcpy(mapped, indexData.data(), stagingDesc.size);
    wgpuBufferUnmap(stagingBuffer);

    WGPUCommandEncoderDescriptor encoderDesc{};
    encoderDesc.nextInChain = nullptr;

    SET_WGPU_LABEL(encoderDesc, "Upload Index Encoder");

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_Device, &encoderDesc);

    wgpuCommandEncoderCopyBufferToBuffer(encoder, stagingBuffer, 0, m_IndexBuffer, 0, stagingDesc.size);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(m_Queue, 1, &cmd);

    //Staging buffer is not required anymore
    wgpuBufferRelease(stagingBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuCommandBufferRelease(cmd);


    // Upload geometry data to the buffer
    //wgpuQueueWriteBuffer(m_Queue, m_Buffer1, 0, vertexData.data(), bufferDesc.size);

    return true;
}

bool Application::CreateUniformBuffer()
{
    WGPUBufferDescriptor uniformBufferDesc{};
    uniformBufferDesc.nextInChain = nullptr;
    uniformBufferDesc.size = m_ConstantUniformBufferSize + m_DynamicsUniformBufferSize;
    uniformBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform; // GPU-only
    uniformBufferDesc.mappedAtCreation = false;

    m_UniformBuffer = wgpuDeviceCreateBuffer(m_Device, &uniformBufferDesc);

    ConstantUniforms ConstData;
    FillConstantUniform(ConstData, Pandu::Matrix44::IDENTITY, Pandu::Matrix44::IDENTITY, 0.0f, 0.0f);
    wgpuQueueWriteBuffer(m_Queue, m_UniformBuffer, 0, &ConstData, sizeof(ConstantUniforms));

    DynamicUniforms DynData;
    FillDynamicUniform(DynData, Pandu::Matrix44::IDENTITY, Pandu::Vector4::UNIT);
    wgpuQueueWriteBuffer(m_Queue, m_UniformBuffer, m_ConstantUniformBufferStride, &DynData, sizeof(DynamicUniforms));

    // Create a binding
    WGPUBindGroupEntry binding[2];
    binding[0].nextInChain = nullptr;
    binding[0].binding = 0;
    binding[0].buffer = m_UniformBuffer;
    binding[0].offset = 0;
    binding[0].size = sizeof(ConstantUniforms); // 32;
    binding[0].sampler = nullptr;
    binding[0].textureView = nullptr;

    binding[1].nextInChain = nullptr;
    binding[1].binding = 1;
    binding[1].buffer = m_UniformBuffer;
    binding[1].offset = m_ConstantUniformBufferStride;
    binding[1].size = sizeof(DynamicUniforms); ;
    binding[1].sampler = nullptr;
    binding[1].textureView = nullptr;


    // A bind group contains one or multiple bindings
    WGPUBindGroupDescriptor bindGroupDesc{};
    bindGroupDesc.nextInChain = nullptr;
    bindGroupDesc.layout = m_BindGroupLayout;
    // There must be as many bindings as declared in the layout!
    bindGroupDesc.entryCount = 2;
    bindGroupDesc.entries = binding;
    m_BindGroup = wgpuDeviceCreateBindGroup(m_Device, &bindGroupDesc);

    return true;
}