#pragma once
#include <stdafx.h>
#include <DXSample.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class D3DApp : public DXSample
{
	static const UINT num_frames = 2;

	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT4 color;
	};

	//	Pipeline Objects
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissor_rect;
	ComPtr<IDXGISwapChain4> m_swap_chain;
	ComPtr<ID3D12Device2> m_device;
	ComPtr<ID3D12Resource> m_render_targets[num_frames];
	ComPtr<ID3D12CommandAllocator> m_command_allocator;
	ComPtr<ID3D12CommandAllocator> m_bundle_allocator;
	ComPtr<ID3D12CommandQueue> m_command_queue;
	ComPtr<ID3D12RootSignature> m_root_signature;
	ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
	ComPtr<ID3D12PipelineState> m_pipeline_state;
	ComPtr<ID3D12GraphicsCommandList> m_command_list;
	ComPtr<ID3D12GraphicsCommandList> m_bundle;
	UINT m_rtv_descriptor_size;
	
	//	Resources
	D3D12_VERTEX_BUFFER_VIEW m_vertex_buffer_view;
	ComPtr<ID3D12Resource> m_vertex_buffer;

	//	Sy=nchronization Objects
	UINT m_frame_index;
	HANDLE m_fence_event;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fence_value;

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

