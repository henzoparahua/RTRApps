#include "D3DApp.h"

D3DApp::D3DApp(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    frameIndex(0),
    m_rtvDescriptorSize(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height))
{

}

void D3DApp::OnInit()
{
    LoadPipeline();
    LoadAssets();
}

void D3DApp::LoadPipeline()
{
    UINT dxgiFactoryFlags{ 0 };

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug1> debugController;
        D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) >> chk;
        debugController->EnableDebugLayer();
        debugController->SetEnableGPUBasedValidation(true);
    }
#endif

    ComPtr<IDXGIFactory7> factory;
    CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)) >> chk;

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)) >> chk;

        D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_12_1,
            IID_PPV_ARGS(&m_device)
        ) >> chk;
    }
    else {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_12_1,
            IID_PPV_ARGS(&m_device)
        ) >> chk;
    }

    //  Create and Describe the Command Queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)) >> chk;

    //  Describe and create the Swap Chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = NumFrames;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        Win32App::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ) >> chk;


    factory->MakeWindowAssociation(Win32App::GetHwnd(), DXGI_MWA_NO_ALT_ENTER) >> chk;

    swapChain.As(&m_swapChain) >> chk;
    frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    //  Create Descriptor Heap
    {
        //  Describe and Create a render target view (RTV) descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = NumFrames;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)) >> chk;

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    //  Create Frame Resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        //  Create a RTV for each frame.
        for (UINT n{ 0 }; n < NumFrames; n++)
        {
            m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])) >> chk;
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_commandAllocator)
    ) >> chk;
}

void D3DApp::LoadAssets()
{
    //  Create an empty Root Signature
    {
        CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
        root_signature_desc.Init(0, nullptr, 0, nullptr, 
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        
        D3D12SerializeRootSignature(
            &root_signature_desc, 
            D3D_ROOT_SIGNATURE_VERSION_1, 
            &signature, &error) >> chk;

        m_device->CreateRootSignature(
            0, signature->GetBufferPointer(),
            signature->GetBufferSize(), 
            IID_PPV_ARGS(&m_rootSignature)) >> chk;
    }

    //  Create the Pipeline State, which includes compiling and loading shaders.
    {
        UINT8* vertex_shader_data = nullptr;
        UINT8* pixel_shader_data = nullptr;
        UINT vertex_shader_data_length = 0;
        UINT pixel_shader_data_length = 0;

        ReadDataFromFile(
            GetAssetFullPath(L"shaders_VSMain.cso").c_str(),
            &vertex_shader_data,
            &vertex_shader_data_length
        ) >> chk;

        ReadDataFromFile(
            GetAssetFullPath(L"shaders_PSMain.cso").c_str(),
            &pixel_shader_data,
            &pixel_shader_data_length
        ) >> chk;

        //  Define the Vertex Input Layout
        D3D12_INPUT_ELEMENT_DESC input_element_descs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        //  Describe and Create the PSO
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { input_element_descs, _countof(input_element_descs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader_data, vertex_shader_data_length);
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader_data, pixel_shader_data_length);
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)) >> chk;
    }

    //  Create the Command List
    m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandList)
    ) >> chk;

    m_commandList->Close() >> chk;

    //  Create the Vertex Buffer
    {
        //  Define the geometry for a triangle
        Vertex triangle_vertices[] =
        {
            { { 0.0f, 0.3f * m_aspectRatio, 0.0f }, { 0.275f, 0.835f, 1.0f, 1.0f } },
            { { 0.4f, -0.3f * m_aspectRatio, 0.0f }, { 0.875f, 0.61f, 0.93f, 1.0f } },
            { { -0.4f, -0.3f * m_aspectRatio, 0.0f }, { 0.0f, 0.9f, 0.9f, 0.5f}}
        };

        const UINT vertex_buffer_size = sizeof(triangle_vertices);

        //  TODO: CHANGE THE UPLOAD HEAP (HERE IT TRANSFER STATIC DATA IN NON-RECOMMENDED WAY)

        auto heap_type_upload{ CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD) };
        auto vbs_desc{ CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size) };

        m_device->CreateCommittedResource(
            &heap_type_upload,
            D3D12_HEAP_FLAG_NONE,
            &vbs_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)
        ) >> chk;

        //  Copy the triangle data to the vertex buffer
        UINT8* vertex_data_begin;
        CD3DX12_RANGE read_range(0, 0); 
        m_vertexBuffer->Map(0, &read_range, reinterpret_cast<void**>(&vertex_data_begin)) >> chk;
        memcpy(vertex_data_begin, triangle_vertices, sizeof(triangle_vertices));
        m_vertexBuffer->Unmap(0, nullptr);

        //  Initialize the VBV 
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertex_buffer_size;
    }

    //  Create Synchronization Objects
    {
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)) >> chk;
        fenceValue = 1;

        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent == nullptr)
        {
            HRESULT_FROM_WIN32(GetLastError()) >> chk;
        }

        WaitForPreviousFrame();
    }
}

//  Update frame-based values.
void D3DApp::OnUpdate()
{

}

//  Render this scene.
void D3DApp::OnRender()
{
    PopulateCommandList();

    ID3D12CommandList* ppCommandList[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

    m_swapChain->Present(1, 0) >> chk;

    WaitForPreviousFrame();
}

void D3DApp::OnDestroy()
{
    //  Ensure the GPU is no longer referencing resources that are about to be cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(fenceEvent);
}

void D3DApp::PopulateCommandList()
{
    m_commandAllocator->Reset() >> chk;

    m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()) >> chk;

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    //  Indicate that the back buffer will be used as a render target.
    {
        auto rtvState{ CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        ) };

        m_commandList->ResourceBarrier(1, &rtvState);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        frameIndex,
        m_rtvDescriptorSize
    );

    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    //  Record Commands.
    const float clearColor[] = { 0.1f, 0.1f, 0.15f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    //  Indicate that the back buffer will be used to present.
    {
        auto rtvState{ CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        ) };

        m_commandList->ResourceBarrier(1, &rtvState);
    }

    m_commandList->Close() >> chk;
}

void D3DApp::WaitForPreviousFrame()
{
    //  Wait for a frame to complete before continuing is not best practice.

    //  Signal and increment the fence value.
    const UINT64 fence{ fenceValue };
    m_commandQueue->Signal(m_fence.Get(), fence) >> chk;
    fenceValue++;

    if (m_fence->GetCompletedValue() < fence)
    {
        m_fence->SetEventOnCompletion(fence, fenceEvent) >> chk;
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}