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

	struct SceneConstantBuffeer
	{
		XMFLOAT4 offset;
		float padding[60];
	};
	static_assert((sizeof(SceneConstantBuffeer) % 256) == 0, "Constant Buffer size must be 256-byte aligned.");

	//	Pipeline Objects
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissor_rect;
	ComPtr<IDXGISwapChain4> m_swap_chain;
	ComPtr<ID3D12Device2> m_device;
	ComPtr<ID3D12Resource>  m_render_targets[num_frames];
	ComPtr<ID3D12CommandAllocator> m_command_allocator;
	ComPtr<ID3D12CommandQueue> m_command_queue;
	ComPtr<ID3D12RootSignature1> m_root_signature;
	ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
	ComPtr<ID3D12DescriptorHeap> m_cbv_heap;
	ComPtr<ID3D12PipelineState> m_pipeline_state;
	ComPtr<ID3D12GraphicsCommandList> m_command_list;
	UINT m_rtv_descriptor_size;

	//	Resources
	ComPtr<ID3D12Resource> m_vertex_buffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertex_buffer_view;
	ComPtr<ID3D12Resource> m_constant_buffer;
	SceneConstantBuffeer m_constant_buffer_data;
	UINT8* m_cbv_data_begin;

	//	Syncronization Objects
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
