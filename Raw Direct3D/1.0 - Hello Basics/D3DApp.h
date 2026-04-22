#pragma once
#include <stdafx.h>
#include <DXSample.h>
#include "StepTimer.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class D3DApp : public DXSample {
	static const INT num_frames{ 2 };

	struct Vertex {
		XMFLOAT3 position;
		XMFLOAT4 color;
	};

	struct SceneConstantBuffer {
		XMFLOAT4 offset;
		float padding[60];
	};
	static_assert((sizeof(SceneConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	//	Pipeline Objects
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissor_rect;
	ComPtr<IDXGISwapChain4> m_swap_chain;
	ComPtr<ID3D12Device4> m_device;
	ComPtr<ID3D12Resource2> m_render_targets[num_frames];
	ComPtr<ID3D12CommandAllocator> m_command_allocators[num_frames];
	ComPtr<ID3D12CommandAllocator> m_bundle_allocators[num_frames];
	ComPtr<ID3D12CommandQueue> m_command_queue;
	ComPtr<ID3D12RootSignature> m_root_signature;
	ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
	ComPtr<ID3D12DescriptorHeap> m_cbv_heap;
	ComPtr<ID3D12PipelineState> m_pipeline_state;
	ComPtr<ID3D12GraphicsCommandList> m_command_list;
	ComPtr<ID3D12GraphicsCommandList> m_bundle;
	UINT m_rtv_descriptor_size;

	//	Resources
	ComPtr<ID3D12Resource> m_vertex_buffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertex_buffer_view;
	ComPtr<ID3D12Resource> m_constant_buffer;
	SceneConstantBuffer m_constant_buffer_data;
	UINT8* m_cbv_data_begin;
	StepTimer m_timer;

	//	Synchronization Objects
	UINT m_frame_index;
	UINT m_frame_counter;
	HANDLE m_fence_event;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fence_values[num_frames];

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForGPU();
	void MoveToNextFrame();

	void ShowFPS()
	{
		m_timer.Tick(NULL);

		if (m_frame_counter == 500)
		{
			wchar_t fps[64];
			swprintf_s(fps, L"%ufps", m_timer.GetFramesPerSecond());
			SetCustomWindowText(fps);
			m_frame_counter = 0;
		}

		m_frame_counter++;
	}

public:
	D3DApp(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
};