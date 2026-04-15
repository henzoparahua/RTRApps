#include "D3DApp.h"

D3DApp::D3DApp(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_frame_index(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissor_rect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_rtv_descriptor_size(0)
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

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warp_adapter;
		factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)) >> chk;
		D3D12CreateDevice(
			warp_adapter.Get(),
			D3D_FEATURE_LEVEL_12_1,
			IID_PPV_ARGS(&m_device)
		) >> chk;
	}
	else {
		ComPtr<IDXGIAdapter1> hardware_adapter;
		GetHardwareAdapter(factory.Get(), &hardware_adapter);
		D3D12CreateDevice(
			hardware_adapter.Get(),
			D3D_FEATURE_LEVEL_12_1,
			IID_PPV_ARGS(&m_device)
		) >> chk;
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
	
	factory->MakeWindowAssociation(Win32App::GetHwnd(), DXGI_MWA_NO_ALT_ENTER) >> chk;
	swap_chain.As(&m_swap_chain) >> chk;
	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();

	{
		D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
		rtv_heap_desc.NumDescriptors = num_frames;
		rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		m_device->CreateDescriptorHeap(
			&rtv_heap_desc,
			IID_PPV_ARGS(&m_rtv_heap)
		) >> chk;

		m_rtv_descriptor_size = 
			m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	//	Criando os Resources de cada frame
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart());
		
		//	Create a rtv for each frame
		for (UINT n{0}; n < num_frames; n++)
		{
			m_swap_chain->GetBuffer(n, IID_PPV_ARGS(&m_render_targets[n]));
			m_device->CreateRenderTargetView(m_render_targets[n].Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);
		}
	}

	m_device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&m_command_allocator)
	) >> chk;
	m_device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_BUNDLE,
		IID_PPV_ARGS(&m_bundle_allocator)
	);
}

void D3DApp::LoadAssets()
{
	//	Criando o Root Signature
	{
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc{};
		root_signature_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
		root_signature_desc.Desc_1_2.NumParameters = 0;
		root_signature_desc.Desc_1_2.pParameters = nullptr;
		root_signature_desc.Desc_1_2.NumStaticSamplers = 0;
		root_signature_desc.Desc_1_2.pStaticSamplers = nullptr;
		root_signature_desc.Desc_1_2.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;

		D3D12SerializeVersionedRootSignature(
			&root_signature_desc,
			&signature,
			&error
		) >> chk;

		m_device->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(&m_root_signature)
		) >> chk;
	}

	//	Criando o Pipeline State, compilando shaders e carregando esses shaders.
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

		//	Definindo o vertex input layout
		D3D12_INPUT_ELEMENT_DESC input_element_descs[]{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		//	Descrevendo e criando o PSO
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
		pso_desc.InputLayout = { input_element_descs, _countof(input_element_descs) };
		pso_desc.pRootSignature = m_root_signature.Get();
		pso_desc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader_data, vertex_shader_data_length);
		pso_desc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader_data, pixel_shader_data_length);
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

	//	Criando a Command List
	m_device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_command_allocator.Get(),
		m_pipeline_state.Get(),
		IID_PPV_ARGS(&m_command_list)
	) >> chk;
	m_command_list->Close();

	//	Criando o Vertex Buffer
	{
		//	Definindo os valores de cada vertex
		Vertex triangle_vertices[] {
			{ { 0.0f, 0.3f * m_aspectRatio, 0.0f }, { 0.275f, 0.835f, 1.0f, 1.0f } },
			{ { 0.4f, -0.3f * m_aspectRatio, 0.0f }, { 0.875f, 0.61f, 0.93f, 1.0f } },
			{ { -0.4f, -0.3f * m_aspectRatio, 0.0f }, { 0.0f, 0.9f, 0.9f, 0.5f}}
		};

		const UINT vertices_buffer_size = sizeof(triangle_vertices);
	}
}