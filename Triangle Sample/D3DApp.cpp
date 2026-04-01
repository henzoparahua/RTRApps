#include "D3DApp.h"

D3DApp::D3DApp(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    frameIndex(0),
    m_rtvDescriptorSize(0)
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
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory7> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_12_1,
            IID_PPV_ARGS(&m_device)
        ));
    }
    else {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_12_1,
            IID_PPV_ARGS(&m_device)
        ));
    }

    //  Create and Describe the Command Queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(
        &queueDesc, 
        IID_PPV_ARGS(&m_commandQueue)
    ));

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
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        Win32App::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));


    ThrowIfFailed(factory->MakeWindowAssociation(
        Win32App::GetHwnd(), 
        DXGI_MWA_NO_ALT_ENTER)
    );

    ThrowIfFailed(swapChain.As(&m_swapChain));
    frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    //  Create Descriptor Heap
    //  A Descriptor Heap can be thought of as an array of descriptors.
    //  Where each descriptor fully describes an object to the GPU.
    {
        //  Describe and Create a render target view (RTV) descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = NumFrames;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    //  Create Frame Resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        //  Create a RTV for each frame.
        for (UINT n{ 0 }; n < NumFrames; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, 
        IID_PPV_ARGS(&m_commandAllocator)
    ));
}

void D3DApp::LoadAssets()
{
    //  Create the Command List
    ThrowIfFailed(m_device->CreateCommandList(
        0, 
        D3D12_COMMAND_LIST_TYPE_DIRECT, 
        m_commandAllocator.Get(), 
        nullptr, 
        IID_PPV_ARGS(&m_commandList)
    ));

    ThrowIfFailed(m_commandList->Close());

    //  Create Synchronization Objects
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        fenceValue = 1;

        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
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

    ThrowIfFailed(m_swapChain->Present(1, 0));
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
    ThrowIfFailed(m_commandAllocator->Reset());

    ThrowIfFailed(m_commandList->Reset(
        m_commandAllocator.Get(), 
        m_pipelineState.Get()
    ));

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

    //  Record Commands.
    const float clearColor[] = { 0.1f, 0.1f, 0.15f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    //  Indicate that the back buffer will be used to present.
    {
        auto rtvState{ CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        ) };

        m_commandList->ResourceBarrier(1, &rtvState);
    }

    ThrowIfFailed(m_commandList->Close());
}

void D3DApp::WaitForPreviousFrame()
{
    //  Wait for a frame to complete before continuing is not best practice.

    //  Signal and increment the fence value.
    const UINT64 fence{ fenceValue };
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    fenceValue++;

    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, fenceEvent));
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}