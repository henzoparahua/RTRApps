#pragma once
#include <stdafx.h>
#include <DXSample.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class D3DApp : public DXSample
{
	static const UINT NumFrames = 2;

	struct Vertex 
	{
		XMFLOAT3 pos;
		XMFLOAT4 color;
	};
	
	//	Pipeline Objects
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain4> m_swapChain;
	ComPtr<ID3D12Device2> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[NumFrames];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	UINT m_rtvDescriptorSize;

	//	Resources
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

	//	Synchronization objects.
	UINT frameIndex;
	HANDLE fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 fenceValue;


	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForPreviousFrame();

public:
	D3DApp(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
};