#include "D3DApp.h"

D3DApp::D3DApp(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissor_rect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_rtv_descriptor_size(0),
	m_constant_buffer_data{},
	m_frame_index(0),
	m_fence_values{}
{

}

void D3DApp::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

void D3DApp::LoadPipeline()
{
	UINT dxgi_factory_flags{ 0 };
#if defined(_DEBUG)
	{
		ComPtr<ID3D12Debug6> debug_controller;
		D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)) >> chk;
		debug_controller->EnableDebugLayer();
		debug_controller->SetEnableGPUBasedValidation(true);
	}
#endif

	ComPtr<IDXGIFactory7> factory;
	CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory)) >> chk;

	if (m_useWarpDevice) {
		ComPtr<IDXGIAdapter> warp_adapter;
		factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)) >> chk;
		D3D12CreateDevice(
			warp_adapter.Get(),
			D3D_FEATURE_LEVEL_12_1,
			IID_PPV_ARGS(&m_device)
		) >> chk;
	}
	else {
		ComPtr<IDXGIAdapter1> temp_adapter;
		ComPtr<IDXGIAdapter4> hardware_adapter;
		for (
			UINT i{ 0 };
			DXGI_ERROR_NOT_FOUND != factory->EnumAdapterByGpuPreference(
				i,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(&temp_adapter)
			);
			i++
			)
		{
			if (SUCCEEDED(temp_adapter.As(&hardware_adapter))) {
				DXGI_ADAPTER_DESC3 desc;
				hardware_adapter->GetDesc3(&desc);
				
				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { continue; }

				if (SUCCEEDED(D3D12CreateDevice(
					temp_adapter.Get(),
					D3D_FEATURE_LEVEL_12_1,
					IID_PPV_ARGS(&m_device)))
					) {	break; }
			}
		}
	}

	D3D12_COMMAND_QUEUE_DESC queue_desc{};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	m_device->CreateCommandQueue(
		&queue_desc,
		IID_PPV_ARGS(&m_command_queue)
	) >> chk;

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc{};
	swap_chain_desc.BufferCount = num_frames;
	swap_chain_desc.Width = m_width;
	swap_chain_desc.Height = m_height;
	swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.SampleDesc.Count = 1;
	ComPtr<IDXGISwapChain1> swap_chain;
	factory->CreateSwapChainForHwnd(
		m_command_queue.Get(),
		Win32App::GetHwnd(),
		&swap_chain_desc,
		nullptr,
		nullptr,
		&swap_chain
	) >> chk;
	factory->MakeWindowAssociation(
		Win32App::GetHwnd(),
		DXGI_MWA_NO_ALT_ENTER
	) >> chk;

	swap_chain.As(&m_swap_chain) >> chk;
	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();

	//	Descriptor Heaps
	{
		//	RTV
		D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
		rtv_heap_desc.NumDescriptors = num_frames;
		rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		m_device->CreateDescriptorHeap(
			&rtv_heap_desc,
			IID_PPV_ARGS(&m_rtv_heap)
		);
		m_rtv_descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		//	CBV
		D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc{};
		cbv_heap_desc.NumDescriptors = 1;
		cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		m_device->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&m_cbv_heap));
	}

	//	Frame Resources
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart());

		for (UINT n{ 0 }; n < num_frames; n++)
		{
			m_swap_chain->GetBuffer(n, IID_PPV_ARGS(&m_render_targets[n])) >> chk;
			m_device->CreateRenderTargetView(m_render_targets[n].Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&m_command_allocators[n])
			);
			m_device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_BUNDLE,
				IID_PPV_ARGS(&m_bundle_allocator[n])
			);
		}
	}
}

void D3DApp::LoadAssets()
{
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data{};
		static const D3D_ROOT_SIGNATURE_VERSION root_signature_levels[]{
			D3D_ROOT_SIGNATURE_VERSION_1_2, 
			D3D_ROOT_SIGNATURE_VERSION_1_1, 
			D3D_ROOT_SIGNATURE_VERSION_1_0
		};
		for (auto level : root_signature_levels) {
			feature_data.HighestVersion = level;

			if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))))
			{
				break;
			}
		}

		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		CD3DX12_ROOT_PARAMETER1 root_parameters[1];
		
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
		root_parameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_ROOT_SIGNATURE_FLAGS root_signature_flags{
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
		};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_1(
			_countof(root_parameters)
			, root_parameters, 
			0, nullptr, 
			root_signature_flags
		);
		
		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		D3DX12SerializeVersionedRootSignature(
			&root_signature_desc,
			feature_data.HighestVersion,
			&signature, &error
		) >> chk;
		m_device->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(&m_root_signature)
		) >> chk;
	}

	//	Pipeline State e coisa e tal
	{
		UINT8* vertex_shader_data_address{ nullptr };
		UINT8* pixel_shader_data_address{ nullptr };
		UINT vertex_shader_data_length{ 0 };
		UINT pixel_shader_data_length{ 0 };

		ReadDataFromFile(
			GetAssetFullPath(L"shaders_VSMain.cso").c_str(), 
			&vertex_shader_data_address, 
			&vertex_shader_data_length
		) >> chk;
		ReadDataFromFile(
			GetAssetFullPath(L"shaders_PSMain.cso").c_str(),
			&pixel_shader_data_address,
			&pixel_shader_data_length
		) >> chk;
		
		D3D12_INPUT_ELEMENT_DESC input_element_descs[]
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		//	Describe and Create the PSO
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
		pso_desc.InputLayout = { input_element_descs, _countof(input_element_descs) };
		pso_desc.pRootSignature = m_root_signature.Get();
		pso_desc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader_data_address, vertex_shader_data_length);
		pso_desc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader_data_address, pixel_shader_data_length);
		pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		pso_desc.DepthStencilState.DepthEnable = FALSE;
		pso_desc.DepthStencilState.StencilEnable = FALSE;
		pso_desc.SampleMask = UINT_MAX;
		pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso_desc.SampleDesc.Count = 1;
		m_device->CreateGraphicsPipelineState(
			&pso_desc,
			IID_PPV_ARGS(&m_pipeline_state)
		) >> chk;
	}

	m_device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_command_allocators[m_frame_index].Get(),
		m_pipeline_state.Get(),
		IID_PPV_ARGS(&m_command_list)
	) >> chk;
	m_command_list->Close();

	//	Geometria e coisa e tal
	{
		//	Triangle geometry
		Vertex triangle_vertices[]{
			{ { 0.0f, 0.3f * m_aspectRatio, 0.0f }, { 0.275f, 0.835f, 1.0f, 1.0f } },
			{ { 0.4f, -0.3f * m_aspectRatio, 0.0f }, { 0.875f, 0.61f, 0.93f, 1.0f } },
			{ { -0.4f, -0.3f * m_aspectRatio, 0.0f }, { 0.0f, 0.9f, 0.9f, 0.5f } }
		};

		const UINT vertices_buffer_size{ sizeof(triangle_vertices) };

		{
			auto heap_properties{ CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD) };
			auto resource_desc_size{ CD3DX12_RESOURCE_DESC::Buffer(vertices_buffer_size) };
			m_device->CreateCommittedResource(
				&heap_properties,
				D3D12_HEAP_FLAG_NONE,
				&resource_desc_size,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_vertex_buffer)
			) >> chk;
		}

		UINT8* vertex_data_begin;
		CD3DX12_RANGE read_range(0, 0);
		m_vertex_buffer->Map(0, &read_range, reinterpret_cast<void**>(&vertex_data_begin)) >> chk;
		memcpy(vertex_data_begin, triangle_vertices, sizeof(triangle_vertices));
		m_vertex_buffer->Unmap(0, nullptr);

		m_vertex_buffer_view.BufferLocation = m_vertex_buffer->GetGPUVirtualAddress();
		m_vertex_buffer_view.SizeInBytes = vertices_buffer_size;
		m_vertex_buffer_view.StrideInBytes = sizeof(Vertex);
	}

	//	Constant Buffer
	{
		const UINT constant_buffer_size = sizeof(SceneConstantBuffer);
		auto resource_desc{ CD3DX12_RESOURCE_DESC::Buffer(constant_buffer_size) };
		auto heap_properties{ CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD) };
		m_device->CreateCommittedResource(
			&heap_properties,
			D3D12_HEAP_FLAG_NONE,
			&resource_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_constant_buffer)
		) >> chk;

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
		cbv_desc.BufferLocation = m_constant_buffer->GetGPUVirtualAddress();
		cbv_desc.SizeInBytes = constant_buffer_size;
		m_device->CreateConstantBufferView(&cbv_desc, m_cbv_heap->GetCPUDescriptorHandleForHeapStart());

		CD3DX12_RANGE read_range(0, 0);
		m_constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&m_cbv_data_begin)) >> chk;
		memcpy(m_cbv_data_begin, &m_constant_buffer_data, sizeof(m_constant_buffer_data));
	}

	//	Gravando o Bundle
	{
		m_device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_BUNDLE,
			m_bundle_allocator[m_frame_index].Get(),
			m_pipeline_state.Get(),
			IID_PPV_ARGS(&m_bundle)
		);
		m_bundle->SetGraphicsRootSignature(m_root_signature.Get());
		m_bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_bundle->IASetVertexBuffers(0, 1, &m_vertex_buffer_view);
		m_bundle->DrawInstanced(3, 1, 0, 0);
		m_bundle->Close() >> chk;
	}

	//	Fence Sync
	{
		m_device->CreateFence(m_fence_values[m_frame_index], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)) >> chk;
		m_fence_values[m_frame_index]++;
		m_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fence_event == nullptr) {
			HRESULT_FROM_WIN32(GetLastError()) >> chk;
		}

		WaitForGPU();
	}
}

void D3DApp::OnUpdate()
{
	const float translation_speed{ 0.0016f };
	const float offset_bounds{ 0.32f };
	static bool direction{ false };

	if (m_constant_buffer_data.offset.x > offset_bounds)
	{
		direction = true;
	}
	if (m_constant_buffer_data.offset.x < -offset_bounds)
	{
		direction = false;
	}

	if (direction) {
		m_constant_buffer_data.offset.x -= translation_speed;
	}
	else {
		m_constant_buffer_data.offset.x += translation_speed;
	}

	memcpy(m_cbv_data_begin, &m_constant_buffer_data, sizeof(m_constant_buffer_data));
}

void D3DApp::OnRender()
{
	PopulateCommandList();

	ID3D12CommandList* command_lists_addresses[]{ m_command_list.Get() };
	m_command_queue->ExecuteCommandLists(_countof(command_lists_addresses), command_lists_addresses);
	
	m_swap_chain->Present(1, 0) >> chk;
	
	MoveToNextFrame();
}

void D3DApp::OnDestroy()
{
	WaitForGPU();
	CloseHandle(m_fence_event);
}

void D3DApp::PopulateCommandList()
{
	m_command_allocators[m_frame_index]->Reset() >> chk;
	m_command_list->Reset(m_command_allocators[m_frame_index].Get(), m_pipeline_state.Get()) >> chk;

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());
	ID3D12DescriptorHeap* heaps[]{ m_cbv_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(heaps), heaps);
	m_command_list->SetGraphicsRootDescriptorTable(0, m_cbv_heap->GetGPUDescriptorHandleForHeapStart());

	m_command_list->RSSetViewports(1, &m_viewport);
	m_command_list->RSSetScissorRects(1, &m_scissor_rect);

	{
		auto resource_barrier{
			CD3DX12_RESOURCE_BARRIER::Transition(
				m_render_targets[m_frame_index].Get(),
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			) 
		};
		m_command_list->ResourceBarrier(1, &resource_barrier);
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
		m_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
		m_frame_index,
		m_rtv_descriptor_size
	);

	m_command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
	const float clearColor[] = { 0.1f, 0.1f, 0.15f, 1.0f };
	m_command_list->ClearRenderTargetView(rtv_handle, clearColor, 0, nullptr);
	m_command_list->ExecuteBundle(m_bundle.Get());
	{
		auto resource_barrier{
			CD3DX12_RESOURCE_BARRIER::Transition(
				m_render_targets[m_frame_index].Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PRESENT
			)
		};
		m_command_list->ResourceBarrier(1, &resource_barrier);
	}

	m_command_list->Close() >> chk;
}

void D3DApp::WaitForGPU()
{
	m_command_queue->Signal(m_fence.Get(), m_fence_values[m_frame_index]) >> chk;

	m_fence->SetEventOnCompletion(m_fence_values[m_frame_index], m_fence_event) >> chk;
	WaitForSingleObjectEx(m_fence_event, INFINITE, FALSE);

	m_fence_values[m_frame_index]++;
}

void D3DApp::MoveToNextFrame()
{
	const UINT64 current_fence_value{ m_fence_values[m_frame_index] };
	m_command_queue->Signal(m_fence.Get(), current_fence_value);
	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();

	if (m_fence->GetCompletedValue() < m_fence_values[m_frame_index])
	{
		m_fence->SetEventOnCompletion(m_fence_values[m_frame_index], m_fence_event) >> chk;
		WaitForSingleObjectEx(m_fence_event, INFINITE, FALSE);
	}

	m_fence_values[m_frame_index] = current_fence_value + 1;
}