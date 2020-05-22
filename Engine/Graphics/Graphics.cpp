#include "Graphics.h"
#include "DXRHelpers/DXRHelper.h"
#include "DXRHelpers/nv_helpers_dx12/BottomLevelASGenerator.h"
#include "DXRHelpers/nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "DXRHelpers/nv_helpers_dx12/RootSignatureGenerator.h"
#include "DXRHelpers/nv_helpers_dx12/ShaderBindingTableGenerator.h"
#include <stdexcept>
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "d3d11.lib")


using Microsoft::WRL::ComPtr;

bool Graphics::Initialize(HWND hwnd, int width, int height)
{
	windowWidth = width;
	windowHeight = height;

	if (!InitializeDirect3D12(hwnd))
		return false;

	CreateRaytracingPipeline();
	CreateRaytracingOutputBuffer();
	CreateShaderResourceHeap();
	CreateShaderBindingTable();


	if (!InitializeScene())
		return false;

	cube.Initialize("Resources\Models\Dandelion\Var1", pDevice.Get(), pCommandList.Get(), cb_vertexShader);

	return true;
}

void Graphics::RenderFrame()
{
	HRESULT hr;

	UpdatePipeline(); // Update the pipeline by sending commands to the commandqueue

	// Create an array of command list (only one command list here)
	ID3D12CommandList* ppCommandLists[] = { pCommandList.Get() };

	// Execute the array of command lists
	pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// This command goes in at the end of out command queue. We will know when our command queue
	// has finished becasue the fence value will be set to "fenceValue" from the GPU since the 
	// command queue is being executed on the GPU
	hr = pCommandQueue->Signal(pFence[frameIndex].Get(), fenceValue[frameIndex]);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Command queue failed to signal");
		Running = false;
	}

	// Present the current backbuffer
	hr = pSwapChain->Present(0, 0);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Swapchain failed to present");
		Running = false;
	}
}

bool Graphics::InitializeDirect3D12(HWND hwnd)
{
	HRESULT hr;

	// -- Create device -- //
	IDXGIFactory4* dxgiFactory;
	hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	if (FAILED(hr))
		return false;

	std::vector<AdapterData> adapters = AdapterReader::GetAdapters();
	hr = D3D12CreateDevice(adapters[1].pAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&pDevice));
	if (FAILED(hr))
		ErrorLogger::Log(hr, "Failed to Create D3D12 device");

	// -- Create Swapchain -- //
	DXGI_MODE_DESC backBufferDesc = {}; // this is to describe our display mode
	backBufferDesc.Width = windowWidth; // buffer width
	backBufferDesc.Height = windowHeight; // buffer height
	//backBufferDesc.Format = DXGI_FORMAT_R8B8G8A8_UNORM; // format of the buffer (rgba 32 bits, 8 bits for each channel)

	// describe our multi-sampling. We are not multi-sampling, so we set the count to 1 (we need at least one sample of course)
	DXGI_SAMPLE_DESC sampleDesc = {};
	sampleDesc.Count = 1; // multisample count (no multisampling, so we just put 1, since we still need 1 sample)

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	hr = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommandQueue));
	if (FAILED(hr)) {
		MessageBox(0, L"Failed to Create Command Queue", L"Error", MB_OK);
	}

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = frameBufferCount;
	swapChainDesc.Width = windowWidth;
	swapChainDesc.Height = windowHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc = sampleDesc;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain{};
	hr = dxgiFactory->CreateSwapChainForHwnd(pCommandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain);
	if (FAILED(hr)) {
		MessageBox(0, L"Failed to Create Swap Chain", L"Error", MB_OK);
	}
	hr = dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
	if (FAILED(hr)) {
		MessageBox(0, L"Failed to Make Window Association", L"Error", MB_OK);
	}
	hr = swapChain.As(&pSwapChain);
	if (FAILED(hr)) {
		MessageBox(0, L"Failed to Cast ComPtr", L"Error", MB_OK);
	}

	frameIndex = pSwapChain->GetCurrentBackBufferIndex();

	// -- Create the Back Buffer (render target views) Descriptor Heap -- //

	// Describe an rtv descriptor heap and create
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = frameBufferCount; // Number of descriptors for this heap
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // This heap is a render target view heap

	// This heal will not be directly references by the shaders (not visible), as this will store the output from the pipleline
	//otherwise we would set the heap's flag to D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&pRtvDescriptorHeap));
	if (FAILED(hr)) {
		MessageBox(0, L"Failed to Create Descriptor Heap", L"Error", MB_OK);
		return false;
	}

	// Get the size of the descriptor in this heal (this is a rtv heap, so only rtv descriptors should be stored in it.
	// Descriptor sizes may vary from device to device, which is whythere is no set size and we must ask the device to 
	// give us the size. We will use the size to increment a descriptor handle offset
	rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Get a handle to the first descriptor in the descriptor heap. A handle is basically a pointer,
	// but we cannot literally use it like a C++ pointer.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(pRtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// Create a RTV for each buffer (double buffering is two buffers, tripple buffering is 3)
	for (int i = 0; i < frameBufferCount; i++)
	{
		// First we get the n'th buffer in the swap chain and store it in the n'th
		// position of out ID3D12Resource array
		hr = pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pRenderTargets[i]));
		if (FAILED(hr)) {
			MessageBox(0, L"Failed to Initialize Render Targets", L"Error", MB_OK);
			return false;
		}

		// The we "create" a render target view which binds the swap chain buffer (ID3D12Resource[n]) to the rtvhandle
		pDevice->CreateRenderTargetView(pRenderTargets[i].Get(), nullptr, rtvHandle);

		// We increment the rtv handle by the rtv descriptor size we got above
		rtvHandle.Offset(1, rtvDescriptorSize);
	}

	// -- Create the Command Allocators -- //
	for (int i = 0; i < frameBufferCount; i++)
	{
		hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAllocators[i]));
		if (FAILED(hr)) {
			MessageBox(0, L"Failed to Create Command Allocator", L"Error", MB_OK);
			return false;
		}
	}

	// Create the command list with the first allocator
	hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCommandAllocators[0].Get(), NULL, IID_PPV_ARGS(&pCommandList));
	if (FAILED(hr))
	{
		MessageBox(0, L"Failed to Create Command List", L"Error", MB_OK);
		return false;
	}

	// -- Create a Fence & Fence Event -- //
	// Create the fences
	for (int i = 0; i < frameBufferCount; i++)
	{
		hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence[i]));
		if (FAILED(hr))
		{
			MessageBox(0, L"Failed to Create Fence", L"Error", MB_OK);
			return false;
		}
		fenceValue[i] = 0; // Set the initial fence value to 0
	}

	// Create a handle to a fence event
	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (fenceEvent == nullptr)
	{
		MessageBox(0, L"Fence Event was nullptr", L"Error", MB_OK);
		return false;
	}

	// --  Create root signature -- //

	// Create a root descriptor. which explains here to find the sata foe this root paramater
	D3D12_ROOT_DESCRIPTOR rootCBVDescriptor;
	rootCBVDescriptor.RegisterSpace = 0;
	rootCBVDescriptor.ShaderRegister = 0;

	// Create a descriptor range (descriptor table) and fill it out
	// this is a range of descriptors inside a descriptor heap
	D3D12_DESCRIPTOR_RANGE descriptorTableRanges[1]; // Only one for right now
	descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // This is a range of shader resource views
	descriptorTableRanges[0].NumDescriptors = 1; // We only have one texture for right now so only 1
	descriptorTableRanges[0].BaseShaderRegister = 0; // Start index of the shader registers in the range
	descriptorTableRanges[0].RegisterSpace = 0; // Space 0. Can usually be 0
	descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // This appends the range to the end of the root signature descriptor tables

	// Create a descriptor table
	D3D12_ROOT_DESCRIPTOR_TABLE descriptorTable;
	descriptorTable.NumDescriptorRanges = _countof(descriptorTableRanges); // We only have one range
	descriptorTable.pDescriptorRanges = &descriptorTableRanges[0]; // The pointer to the beginning of our ranges array

	// Create a root perameter and fill it out
	D3D12_ROOT_PARAMETER rootPerameters[2]; // Only one parameter right now
	rootPerameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // This is a descriptor table
	rootPerameters[0].Descriptor = rootCBVDescriptor; // This is our descriptor table for this root parameter
	rootPerameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // Our Pixel shader will be the only shader accessing this parameter for now

																		 // Fill out the parameter for our desctipor table. Remember its a good idea to sort parametes by frequency of change. Our constant 
	// buffer will be changed multiple times per frame, while our desciptor table will not ba changed at all (in this tutorial)
	rootPerameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // This is a descriptor table
	rootPerameters[1].DescriptorTable = descriptorTable; // This is our descriptor table for this root parameter
	rootPerameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // Our pixel shader will be the only shader accesing this parameter for now

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(_countof(rootPerameters),// We have one root parameter
		rootPerameters, // A pointer to the beginning of our root parameters array
		1,
		&sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | // We can deny shader stages here for better performance
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
	);



	ID3DBlob* signature;
	hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to Serialize Root Signature");
		return false;
	}

	hr = pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRootSignature));
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to create root signature");
		return false;
	}

	// -- Create vertex and pixel shaders -- //

	// When debugging , we can compile the shader at runtime.
	// But for release versions, we can compile the hlsl shaders
	// with fxc.exe to create .cso file, which contian the shader
	// bytecode. We can load the .cso files at runtime to get the 
	// shader bytecode, which of course is faster than compiling 
	// them at runtime

	// Compile vertex shader
	ID3DBlob* vertexShader; // d3d blob for holding vertex shader bytecode
	ID3DBlob* errorBuffer; // A buffer holding the error data if any
	hr = D3DCompileFromFile(L"VertexShader.hlsl",
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&vertexShader,
		&errorBuffer);

	if (FAILED(hr))
	{
		OutputDebugStringA((char*)errorBuffer->GetBufferPointer());
		ErrorLogger::Log(hr, "Failed to compile Vertex shader");
		return false;
	}

	// Fill out a shader bytecode structure, which is basically just a pointer
	// to the shader bytecode and the size of the shader bytecode
	D3D12_SHADER_BYTECODE vertexShaderBytecode = {};
	vertexShaderBytecode.BytecodeLength = vertexShader->GetBufferSize();
	vertexShaderBytecode.pShaderBytecode = vertexShader->GetBufferPointer();

	// Compile shader
	ID3DBlob* pixelShader;
	hr = D3DCompileFromFile(L"PixelShader.hlsl",
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&pixelShader,
		&errorBuffer);
	if (FAILED(hr))
	{
		OutputDebugStringA((char*)errorBuffer->GetBufferPointer());
		ErrorLogger::Log(hr, "Failed to compile Pixel shader");
		return false;
	}

	// Fill Out shader bytecode structure for pixel shader
	D3D12_SHADER_BYTECODE pixelShaderBytecode = {};
	pixelShaderBytecode.BytecodeLength = pixelShader->GetBufferSize();
	pixelShaderBytecode.pShaderBytecode = pixelShader->GetBufferPointer();

	// Create Input layout

	// The input layout is used by the Input Assembler so that it knows
	// how to read the vertex data bound to it.

	D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// Fill out an inpu layout description structure
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};

	// We can get the number of elements in an array by "sizeof(array) / sizeof(arrayElementType)"

	inputLayoutDesc.NumElements = sizeof(inputLayout) / sizeof(D3D12_INPUT_ELEMENT_DESC);
	inputLayoutDesc.pInputElementDescs = inputLayout;

	cb_vertexShader.Initialize(pDevice.Get(), pCommandList.Get());

	// Create a pipleline state object (PSO)

	// In a real application, you will have many pso's. For each diferent shader
	// or in difference combinations of shaders, differenc blend states or different rasterizer states,
	// different topology types (point, line, triangle patch), or different numberof render targets
	// you will need a pso

	// VS is the only required shader for the pso. You might be wondering whan a case would be where
	// you only set the VS. It's possible that you have a pso that only outpus data with the stream
	// output, and not on a render target, which means you would not need anything after the stream output

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {}; // A structure to define a pso
	psoDesc.InputLayout = inputLayoutDesc; // The structure describing out input layout
	psoDesc.pRootSignature = pRootSignature.Get(); // The root signature that describes the input data this pso needs
	psoDesc.VS = vertexShaderBytecode; // Structure describing where to find the vertex shader bytecode and how large it is
	psoDesc.PS = pixelShaderBytecode; // Same as VS but for the pixel shader
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // Type of topology we are drawing
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // Format of the render target
	psoDesc.SampleDesc = sampleDesc;
	psoDesc.SampleMask = 0xffffffff; // Sample mask has to do with multi-sampling. 0xffffffff means point sampling is done
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // A default rasterizer state
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // A default blend state
	psoDesc.NumRenderTargets = 1; // We are only binding one render target
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // A default stencil state
	//psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	// Create the PSO
	hr = pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pPipelineStateObject));
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to create pipleline state object");
		return false;
	}

	// Create vertex buffer

	// A triangle
	// a triangle
	Vertex3D vList[] = {
		// front face
		{ -0.5f,  0.5f, -0.5f, 1.0f, 0.0f, 0.0f },
		{  0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f },
		{ -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f },

		// right side face
		{  0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f },
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 1.0f },
		{  0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f },

		// left side face					    
		{ -0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f },
		{ -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f },

		// back face						    
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0.0f },
		{ -0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 1.0f },
		{  0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f },

		// top face
		{ -0.5f,  0.5f, -0.5f, 1.0f, 0.0f, 0.0f },
		{ 0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 1.0f },
		{ 0.5f,  0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f },

		// bottom face
		{  0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f },
		{  0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0.0f },
	};
	int vBufferSize = sizeof(vList);

	// Create default heap
	// Default heap is memeory on the GPU. Only the GPU has access to this memory
	// To get data into the heap, we will have to upload the data using
	// an upload heap
	pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // A default heap
		D3D12_HEAP_FLAG_NONE, // No flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // Resource description for a buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // We will start this heap in the copy destination state since we will copy
										// will copy data from the upload hea to this heap
		nullptr, // Optomized clear value must be null for this type of resource. Used for render targets and depth/stencil buffers
		IID_PPV_ARGS(&pVertexBuffer)
	);

	// We can give resource heaps a name so we debug with the graphics debugger we know what resource we are looking at
	pVertexBuffer->SetName(L"Vertex Buffer Resource Heap");

	// Create upload heap
	// Upload heaps are used to upload data to the GPU. CPU can write to it, GPU can read from it
	// We will upload the vertex buffer using this heap to the deafault heap
	ID3D12Resource* vBufferUploadHeap;
	pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // Upload heap
		D3D12_HEAP_FLAG_NONE, // No Flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // Resource description for a buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // GPU will read form this buffer and copy it's contents to the default heap
		nullptr,
		IID_PPV_ARGS(&vBufferUploadHeap)
	);
	vBufferUploadHeap->SetName(L"Vetex Buffer Upload Resource Heap");

	// Store Vertes buffer in upload heap
	D3D12_SUBRESOURCE_DATA vertexData = {};
	vertexData.pData = reinterpret_cast<BYTE*>(vList); // Pointer to our vertex array
	vertexData.RowPitch = vBufferSize; // Soze of our triangle vertex data
	vertexData.SlicePitch = vBufferSize; // Also the size of our triangle vertex data

	// We are now creating a command with the command list to copu the data from
	// the upload heap to the default heap
	UpdateSubresources(pCommandList.Get(), pVertexBuffer.Get(), vBufferUploadHeap, 0, 0, 1, &vertexData);

	// Transition the vertex buffer data from copy destination state to verte buffer state
	pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

	// Create Index Buffer

	// A quad (2 triangles)
	DWORD iList[] = {
		// front face
		0, 1, 2, // first triangle
		0, 3, 1, // second triangle

		// left face
		4, 5, 6, // first triangle
		4, 7, 5, // second triangle

		// right face
		8, 9, 10, // first triangle
		8, 11, 9, // second triangle

		// back face
		12, 13, 14, // first triangle
		12, 15, 13, // second triangle

		// top face
		16, 17, 18, // first triangle
		16, 19, 17, // second triangle

		// bottom face
		20, 21, 22, // first triangle
		20, 23, 21, // second triangle
	};

	int iBufferSize = sizeof(iList);

	numCubeIndices = sizeof(iList) / sizeof(DWORD);

	// Create default heap to hold index buffer
	pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // a default heap
		D3D12_HEAP_FLAG_NONE, // no flags
		&CD3DX12_RESOURCE_DESC::Buffer(iBufferSize), // resource description for a buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // start in the copy destination state
		nullptr, // optimized clear value must be null for this type of resource
		IID_PPV_ARGS(&pIndexBuffer));

	// We can give resource hesaps a name so when we debug with the graphcs debugger we know what resources we are looking at
	pIndexBuffer->SetName(L"Index Buffer Resource Heap");

	// Create upload heap to upload index buffer
	ID3D12Resource* iBufferUploadHeap;
	pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // upload heap
		D3D12_HEAP_FLAG_NONE, // no flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // resource description for a buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // GPU will read from this buffer and copy its contents to the default heap
		nullptr,
		IID_PPV_ARGS(&iBufferUploadHeap));
	iBufferUploadHeap->SetName(L"Index buffer Upload Resource Heap");

	// Store vertex buffer in upload heap
	D3D12_SUBRESOURCE_DATA indexData = {};
	indexData.pData = reinterpret_cast<BYTE*>(iList); // pointer to our index array
	indexData.RowPitch = iBufferSize; // size of all our index buffer
	indexData.SlicePitch = iBufferSize; // also the size of our index buffer


	// We are now creating a command with the command list to copy the data from
	// the upload heal to the default
	UpdateSubresources(pCommandList.Get(), pIndexBuffer.Get(), iBufferUploadHeap, 0, 0, 1, &indexData);

	// Transition the vertex buffer data from the copy destination state to vertex buffer state
	pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));

	// -- Create depth/stencil state -- //

	// Create a depth stencil descriptor heap so we ca get a pointer to the depth stencil buffer
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = pDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&pDSDescriptorHeap));
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to create descriptor heap for depth stencil");
		Running = false;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
	depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

	D3D12_CLEAR_VALUE depthOptomizedClearValue = {};
	depthOptomizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthOptomizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptomizedClearValue.DepthStencil.Stencil = 0;

	pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, windowWidth, windowHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptomizedClearValue,
		IID_PPV_ARGS(&pDepthStencilBuffer)
	);
	hr = pDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&pDSDescriptorHeap));
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to create descriptor heap for depth stencil buffer");
		Running = false;
	}
	pDSDescriptorHeap->SetName(L"Depth/Stencil Resource Heap");


	pDevice->CreateDepthStencilView(pDepthStencilBuffer.Get(), &depthStencilDesc, pDSDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart());


	// Create the constant buffer resource heap
	// We will update the constant buffer one or more times per frame, so we will use only an upload heap
	// unlike previously we used an upload heap to upload the vertex and idndex data, and then copied over 
	// to a default heap. If you plan to use a resource for more than a couple of frames, it is usually 
	// more efficient to copy to a default heap where it stays on the GPU. In this case, our constant 
	// buffer will be modified and uploaded at least once per frame so we only use an upload heap

	// First we will create a resource heap (uplaoad heap) for each frame for the cubes ocntant buffers.
	// As you can see we are allocating 64KB for each resource we create. Buffer resource heaps must 
	// be an alignment of 64KB. We are creating 3 resources, one for each frame. Each constant buffer is
	// only a 4x4 matrix of floats in this tutorail. So with a float being 4 bytes, we have 16 floats in one 
	// constant buffer, and we will store 2 constant buffers in each heap, one for each cube, that sony 64x2 bits,
	// or 128 bits we are using for each resource, and each resource must be at least 64KB (65536 bits)

	for (int i = 0; i < frameBufferCount; ++i)
	{
		// create resource for cube 1
		hr = pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // this heap will be used to upload the constant buffer data
			D3D12_HEAP_FLAG_NONE, // no flags
			&CD3DX12_RESOURCE_DESC::Buffer(1024 * 64), // size of the resource heap. Must be a multiple of 64KB for single-textures and constant buffers
			D3D12_RESOURCE_STATE_GENERIC_READ, // will be data that is read from so we keep it in the generic read state
			nullptr, // we do not have use an optimized clear value for constant buffers
			IID_PPV_ARGS(&pConstantBufferUploadHeaps[i]));
		pConstantBufferUploadHeaps[i]->SetName(L"Constant Buffer Upload Resource Heap");

		ZeroMemory(&cbPerObject, sizeof(cbPerObject));

		CD3DX12_RANGE readRange(0, 0);    // We do not intend to read from this resource on the CPU. (so end is less than or equal to begin)

		// map the resource heap to get a gpu virtual address to the beginning of the heap
		hr = pConstantBufferUploadHeaps[i]->Map(0, &readRange, reinterpret_cast<void**>(&pCbvGPUAddress[i]));

		// Because of the constant read alignment requirements, constant buffer views must be 256 bit aligned. Our buffers are smaller than 256 bits,
		// so we need to add spacing between the two buffers, so that the second buffer starts at 256 bits from the beginning of the resource heap.
		memcpy(pCbvGPUAddress[i], &cbPerObject, sizeof(cbPerObject)); // cube1's constant buffer data
		memcpy(pCbvGPUAddress[i] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject)); // cube2's constant buffer data
	}

	D3D12_RESOURCE_DESC textureDesc;
	int imageBytesPerRow;
	BYTE* imageData;
	int imageSize = LoadImageDataFromFile(&imageData, textureDesc, L"Resources\\Textures\\Catalina.jpg", imageBytesPerRow);
	if (imageSize <= 0)
	{
		ErrorLogger::Log(hr, "Failed to load image from file");
		Running = false;
		return false;
	}

	hr = pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // A default heap
		D3D12_HEAP_FLAG_NONE, // No flags
		&textureDesc, // The description of our texture
		D3D12_RESOURCE_STATE_COPY_DEST, // We will copy the texture from the upload heap to here, so we start it out in a copy dest state
		nullptr, // Used for render targets and depth/stencil buffers
		IID_PPV_ARGS(&pTextureBuffer)
	);

	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to create commited resource for texture");
		Running = false;
		return false;
	}
	pTextureBuffer.Get()->SetName(L"Texture Buffer Resource Heap");
	UINT64 textureUploadBufferSize;
	// This function get the size an upload buffer needs to be to upload a texture to the GPU.
	// Each row must be 256 byte aligned exeplt for the last row, which can just be the size in bytes of the row
	// eg. textureUploadBufferSize = ((((width * numBytesPerPixel) + 255) & ~255) * (height - 1)) + (width * numBytesPerPixel);
	// textureUploadBufferSize = (((imageBytesPerRow + 255) & ~255) * (textureDesc.Height - 1)) + imageBytesPerRow;
	pDevice->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &textureUploadBufferSize);

	// now we create an upload heap to upload our texture to the GPU
	hr = pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // Upload heap
		D3D12_HEAP_FLAG_NONE, // no flags
		&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize), // Resource description for a buffer (storeing the image data in this heap just a copy to the default heap)
		D3D12_RESOURCE_STATE_GENERIC_READ, // We will copy the contents from this heap to the default heap above
		nullptr,
		IID_PPV_ARGS(&pTextureBufferUploadHeap)
	);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to commit texture to GPU memory");
		Running = false;
		return false;
	}
	pTextureBufferUploadHeap->SetName(L"Texture Buffer Upload Resource Heap");

	// Store vertex buffer in upload heap
	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = &imageData[0]; // Pointer to our image data
	textureData.RowPitch = imageBytesPerRow; // Size of all our triangle vertex data

	// now we can copy the upload buffer contents to the default heap
	UpdateSubresources(pCommandList.Get(), pTextureBuffer.Get(), pTextureBufferUploadHeap, 0, 0, 1, &textureData);

	// Transition the texture default heap to a pixel shader resource (we will be sampling frrom this heap in the pixel shader to get the color of pixels)
	pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pTextureBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	// Create the descriptor heap that will store our srv
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(pMainDescriptorHeap->GetAddressOf()));
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to create descriptor heap for texture resource");
		Running = true;
		return false;
	}

	// Now we create a shader resource view (descriptor that points to the texture and descripbes it)
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = textureDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	pDevice->CreateShaderResourceView(pTextureBuffer.Get(), &srvDesc, pMainDescriptorHeap->Get()->GetCPUDescriptorHandleForHeapStart());


	// -- Initialize Ry Tracing -- //
	CheckRayTracingSupport();

	CreateAccelerationStructures();











	// -- Always leave this code to be the last in the method becasue this finalizes the values -- // 

   // Now we execute the command list to upload the initial assets (triangle data)
	pCommandList->Close();
	ID3D12CommandList* ppCommandLists[] = { pCommandList.Get() };
	pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Increment the fence value now, otherwise the buffer might not be uploaded by the time we start drawing
	fenceValue[frameIndex]++;
	hr = pCommandQueue->Signal(pFence[frameIndex].Get(), fenceValue[frameIndex]);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed in signal command queue");
	}
	delete imageData;

	// Create vertex buffer view for the triangle. We get the GPU memory address to the vertex pointer using the GetGPUVertualAddress() method
	vertexbufferView.BufferLocation = pVertexBuffer->GetGPUVirtualAddress();
	vertexbufferView.StrideInBytes = sizeof(Vertex3D);
	vertexbufferView.SizeInBytes = vBufferSize;

	// Create a vertex buffer view for the triangle. We get the GPU memory address to the vertex pointer using the GetGPUVirtualAddress() method
	indexBufferView.BufferLocation = pIndexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT; // 32-bit unsigned integer (this is what a dword is, double word, a word is 2 bytes)
	indexBufferView.SizeInBytes = iBufferSize;


	// Fill out the Viewport
	viewPort.TopLeftX = 0;
	viewPort.TopLeftY = 0;
	viewPort.Width = windowWidth;
	viewPort.Height = windowHeight;
	viewPort.MinDepth = 0.0f;
	viewPort.MaxDepth = 1.0f;

	// Fill out Scissor Rect
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = windowWidth;
	scissorRect.bottom = windowHeight;

	// Build projection matrix
	using namespace DirectX;
	XMMATRIX tmpMat = XMMatrixPerspectiveFovLH(45.0f * (3.14f / 180.0f), (float)windowWidth / (float)windowHeight, 0.1f, 1000.0f);
	camera.SetProjectionValues(45.0f, (float)windowWidth / (float)windowHeight, 0.1f, 1000.0f);
	XMMATRIX projectionMat = camera.GetProjectionMatrix();

	//XMStoreFloat4x4(&cameraProjectionMat, tmpMat);

	// Set starting camera state
	//cameraPosition = XMFLOAT4(0.0f, 2.0f, -4.0f, 0.0f);
	//cameraTarget = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	//cameraUp = XMFLOAT4(0.0f, 1.0f, 0.0f, 0.0f);

	//// Build view matrix
	//XMVECTOR cPos = XMLoadFloat4(&cameraPosition);
	//XMVECTOR cTarg = XMLoadFloat4(&cameraTarget);
	//XMVECTOR cUp = XMLoadFloat4(&cameraUp);
	//tmpMat = XMMatrixLookAtLH(cPos, cTarg, cUp);

	//XMStoreFloat4x4(&cameraViewMat, tmpMat);

	// Set starting cubes position
	// First cube
	cube1Position = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f); // Set cube1's position
	XMVECTOR posVec = XMLoadFloat4(&cube1Position); // Create XMVECTOR for cube1's position

	projectionMat = XMMatrixTranslationFromVector(posVec); // Create translation matrix from cube1's position vector
	XMStoreFloat4x4(&cube1RotMat, XMMatrixIdentity()); // Initialize cube1's rotaion matrix to identity matrix
	XMStoreFloat4x4(&cube1WorldMat, projectionMat); // Store cube1's world matrix

	// second cube
	cube2PositionOffset = XMFLOAT4(1.5f, 0.0f, 0.0f, 0.0f);
	posVec = XMLoadFloat4(&cube2PositionOffset) + XMLoadFloat4(&cube1Position); // create xmvector for cube2's position
																				// we are rotating around cube1 here, so add cube2's position to cube1

	projectionMat = XMMatrixTranslationFromVector(posVec); // create translation matrix from cube2's position offset vector
	XMStoreFloat4x4(&cube2RotMat, XMMatrixIdentity()); // initialize cube2's rotation matrix to identity matrix
	XMStoreFloat4x4(&cube2WorldMat, projectionMat); // store cube2's world matrix

	dxgiFactory->Release();

	return true;
}

void Graphics::UpdatePipeline()
{
	HRESULT hr;

	// We have to wait for the GPU to finish withtthe command allocator before we reset it
	WaitForPreviousFrame();
	hr = pCommandAllocators[frameIndex]->Reset();
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to reset command allocator");
		Running = false;
	}

	// Rest the comman list. By resetting the command list we are putting it into a 
	// recording state so we can start recording commands into the command allocator.
	// The command allocator that we reference here may have multiple command lists
	// associated with it, but only one can be recorded at ne time. Make sure that 
	// any othe command lists associated to this command allocator are in the closed
	// state (not recording).
	// Here you will pass an initial pipeline state object as the second parameter,
	// but in this tutorial we are only clearing the rtv, and do not actually need 
	// anything but an initial default pipeline, which is what we get by setting
	// the second parameter to NULL
	hr = pCommandList->Reset(pCommandAllocators[frameIndex].Get(), pPipelineStateObject.Get());
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Fialed to reset command list");
		Running = false;
	}

	// Here we start recording commands into the commandList (which all the commands will be stored in the commandAllocator)

	// Transition the "frameindex" render target from the present state to the render target state so the command list draws to it starting from here
	pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRenderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Here we again get the bhandle to our current render target view so we can set it as th render target in the output merger stage of the pipleline
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(pRtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);

	// Get a handle to the depth/stencil buffer
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(pDSDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart());

	// Set the render target for the output merger stage (the output of the pipleline)
	pCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	

	// Clear the depth/stencil buffer
	pCommandList->ClearDepthStencilView(pDSDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// Set Root signature
	pCommandList->SetGraphicsRootSignature(pRootSignature.Get());

	// Set the descriptor heap
	ID3D12DescriptorHeap* descriptorHeaps[] = { pMainDescriptorHeap->Get() };
	pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	pCommandList->SetGraphicsRootDescriptorTable(1, pMainDescriptorHeap->Get()->GetGPUDescriptorHandleForHeapStart());

	if (m_raster)
	{
		// Clear the render targets by using the ClearRenderTargetView command
		const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
		pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		pCommandList->RSSetViewports(1, &viewPort); // Set the the viewports
		pCommandList->RSSetScissorRects(1, &scissorRect); // Set the scissor rects
		pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // Set the primitive topology
		pCommandList->IASetVertexBuffers(0, 1, &vertexbufferView); // Set the vertex buffer (using the vertex buffer view)
		pCommandList->IASetIndexBuffer(&indexBufferView);
	}
	else
	{
		std::vector<ID3D12DescriptorHeap*> heaps = { m_srvUavHeap.Get() };
		pCommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		pCommandList->ResourceBarrier(1, &transition);

		D3D12_DISPATCH_RAYS_DESC desc = {};
		
		uint32_t rayGenerationSectionSizeInBytes = m_sbtHelper.GetRayGenSectionSize();
		desc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

		uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
		desc.MissShaderTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
		desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
		desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();

		uint32_t hitGroupSectionSize = m_sbtHelper.GetHitGroupSectionSize();
		desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes + missSectionSizeInBytes;
		desc.HitGroupTable.SizeInBytes = hitGroupSectionSize;
		desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();
		desc.Width = windowWidth;
		desc.Height = windowHeight;
		desc.Depth = 1;

		pCommandList->SetPipelineState1(m_rtStateObject.Get());
		pCommandList->DispatchRays(&desc);

		transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		pCommandList->ResourceBarrier(1, &transition);
		transition = CD3DX12_RESOURCE_BARRIER::Transition(pRenderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
		pCommandList->ResourceBarrier(1, &transition);
		pCommandList->CopyResource(pRenderTargets[frameIndex].Get(), m_outputResource.Get());

		transition = CD3DX12_RESOURCE_BARRIER::Transition(pRenderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
		pCommandList->ResourceBarrier(1, &transition);
	}
	
	// First cube
	// Set cube1's constant buffer
	pCommandList->SetGraphicsRootConstantBufferView(0, pConstantBufferUploadHeaps[frameIndex].Get()->GetGPUVirtualAddress());

	// Draw first cube
	pCommandList->DrawIndexedInstanced(numCubeIndices, 1, 0, 0, 0);

	// Second cube
	// Set cube 2's constant buffer. you can wee we are adding the size of ConstantBufferPerObject to the constant buffer
	// resource heapsaddress. This is because cube1's constatnt buffer is stored at the beginning of the resource heap,
	// while cube2's constant buffer data is storeed after (256 bits from the start of the heap).
	pCommandList->SetGraphicsRootConstantBufferView(0, pConstantBufferUploadHeaps[frameIndex].Get()->GetGPUVirtualAddress() + ConstantBufferPerObjectAlignedSize);

	//cube.Draw(camera.GetViewMatrix() * camera.GetProjectionMatrix(), 0, pConstantBufferUploadHeaps[frameIndex].Get()->GetGPUVirtualAddress());

	// Draw second cube
	pCommandList->DrawIndexedInstanced(numCubeIndices, 1, 0, 0, 0);


	// Transition the "framework" render target from the render target state to the present state. If the debug layer is enabled you will recieve a
	// warning if presnet is called on the render target when it's not ins the present state
	pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRenderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	hr = pCommandList->Close();
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to close command list");
		Running = false;
	}
}

bool Graphics::InitializeShaders()
{

	return true;
}

bool Graphics::InitializeScene()
{
	camera.SetPosition(0.0f, 0.0f, -5.0f);
	//camera.SetProjectionValues(90.0f, static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 0.1f, 1000.0f);

	return true;
}

void Graphics::CheckRayTracingSupport()
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	HRESULT hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
	COM_ERROR_IF_FAILED(hr, "Failed ot get feature support result from device for ray tracing");
	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
		throw std::runtime_error("Raytracing not supported on device");
}

Graphics::AccelerationStructureBuffers Graphics::CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers)
{
	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;

	for (const auto& buffer : vVertexBuffers) {
		bottomLevelAS.AddVertexBuffer(buffer.first.Get(), 0, buffer.second,
			5 * sizeof(float), 0, 0);
	}

	UINT64 scratchSizeInBytes = 0;
	UINT64 resultSizeInBytes = 0;

	bottomLevelAS.ComputeASBufferSizes(pDevice.Get(), false, &scratchSizeInBytes,
		&resultSizeInBytes);

	AccelerationStructureBuffers buffers;
	buffers.pScratch = nv_helpers_dx12::CreateBuffer(
		pDevice.Get(), scratchSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON,
		nv_helpers_dx12::kDefaultHeapProps);
	buffers.pResult = nv_helpers_dx12::CreateBuffer(
		pDevice.Get(), resultSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nv_helpers_dx12::kDefaultHeapProps);

	bottomLevelAS.Generate(pCommandList.Get(), buffers.pScratch.Get(),
		buffers.pResult.Get(), false, nullptr);

	return buffers;
}

void Graphics::CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances)
{
	for (size_t i = 0; i < instances.size(); i++)
	{
		m_topLevelASGenerator.AddInstance(instances[i].first.Get(), instances[i].second, static_cast<UINT>(i), static_cast<UINT>(i));
	}

	UINT64 scratchSize, resultSize, instanceDescsSize;
	m_topLevelASGenerator.ComputeASBufferSizes(pDevice.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);

	m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(pDevice.Get(),
		resultSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nv_helpers_dx12::kDefaultHeapProps);
	m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(pDevice.Get(),
		resultSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nv_helpers_dx12::kDefaultHeapProps);

	m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(pDevice.Get(),
		instanceDescsSize,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nv_helpers_dx12::kUploadHeapProps);

	m_topLevelASGenerator.Generate(pCommandList.Get(),
		m_topLevelASBuffers.pScratch.Get(),
		m_topLevelASBuffers.pResult.Get(),
		m_topLevelASBuffers.pInstanceDesc.Get());
}

void Graphics::CreateAccelerationStructures()
{
	AccelerationStructureBuffers bottomLevelBuffers =
		CreateBottomLevelAS({ {pVertexBuffer.Get(), 3} });

	m_instances = { {bottomLevelBuffers.pResult, XMMatrixIdentity()} };
	CreateTopLevelAS(m_instances);
	
	m_bottomLevelAS = bottomLevelBuffers.pResult;
}

ComPtr<ID3D12RootSignature> Graphics::CreateRayGenSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddHeapRangesParameter(
		{ {0 /*u0*/, 1 /*1 descriptor */, 0 /*use the implicit register space 0*/,
		  D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer*/,
		  0 /*heap slot where the UAV is defined*/},
		 {0 /*t0*/, 1, 0,
		  D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*Top-level acceleration structure*/,
		  1} });

	return rsc.Generate(pDevice.Get(), true);
}

ComPtr<ID3D12RootSignature> Graphics::CreateMissSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	return rsc.Generate(pDevice.Get(), true);
}

ComPtr<ID3D12RootSignature> Graphics::CreateHitSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	return rsc.Generate(pDevice.Get(), true);
}

void Graphics::CreateRaytracingPipeline()
{
	nv_helpers_dx12::RayTracingPipelineGenerator pipeline(pDevice.Get());

	m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"RayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Miss.hlsl");
	m_hitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Hit.hlsl");

	pipeline.AddLibrary(m_rayGenLibrary.Get(), { L"RayGen" });
	pipeline.AddLibrary(m_missLibrary.Get(), { L"Miss" });
	pipeline.AddLibrary(m_hitLibrary.Get(), { L"ClosestHit" });

	m_rayGenSignature = CreateRayGenSignature();
	m_missSignature = CreateMissSignature();
	m_hitSignature = CreateHitSignature();

	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");

	pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), { L"RayGen" });
	pipeline.AddRootSignatureAssociation(m_missSignature.Get(), { L"Miss" });
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), { L"HitGroup" });

	pipeline.SetMaxPayloadSize(4 * sizeof(float)); // RGB + distance

	pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates

	pipeline.SetMaxRecursionDepth(1);

	// Compile the pipeline for execution on the GPU
	m_rtStateObject = pipeline.Generate();

	// Cast the state object into a properties object, allowing to later access
	// the shader pointers by name
	HRESULT hr = m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps));
	if (FAILED(hr))
	{
		MessageBoxW(NULL, L"Failed to create ray trace pipeline", L"Error", MB_OK);
	}
}

void Graphics::CreateRaytracingOutputBuffer()
{
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	// The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB
	// formats cannot be used with UAVs. For accuracy we should convert to sRGB
	// ourselves in the shader
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = windowWidth;
	resDesc.Height = windowHeight;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	HRESULT hr = pDevice->CreateCommittedResource(
		&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
		IID_PPV_ARGS(&m_outputResource));
	if (FAILED(hr))
	{
		MessageBoxW(NULL, L"Failed to create create committed resource for ray tracing output buffer", L"Error", MB_OK);
	}
}

void Graphics::CreateShaderResourceHeap()
{
	m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(
		pDevice.Get(), 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
		m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	pDevice->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc,
		srvHandle);

	srvHandle.ptr += pDevice->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location =
		m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
	
	pDevice->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
}

void Graphics::CreateShaderBindingTable()
{
	m_sbtHelper.Reset();

	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();

	auto heapPointer = reinterpret_cast<UINT64*>(srvUavHeapHandle.ptr);
	
	m_sbtHelper.AddRayGenerationProgram(L"RayGen", { heapPointer });

	m_sbtHelper.AddMissProgram(L"Miss", {});

	m_sbtHelper.AddHitGroup(L"HitGroup", {});
	
	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
		pDevice.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	if (!m_sbtStorage) {
		throw std::logic_error("Could not allocate the shader binding table");
	}

	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());

}

int Graphics::LoadImageDataFromFile(BYTE** imageData, D3D12_RESOURCE_DESC& resourceDescription, LPCWSTR filename, int& bytesPerRow)
{
	HRESULT hr;

	// We only ned one instance of the imageing factory to create decoders and frames
	static IWICImagingFactory* pWicFactory;

	// Reset decoder, frame and converter since these will be different for each image we load
	IWICBitmapDecoder* pWicDecoder = NULL;
	IWICBitmapFrameDecode* pWicFrame = NULL;
	IWICFormatConverter* pWicConverter = NULL;

	bool imageConverted = false;
	if (pWicFactory == NULL)
	{
		// Initialize the COM Factory
		hr = CoInitialize(NULL);
		if (FAILED(hr))
		{
			ErrorLogger::Log(hr, "Failed to CoInitialize");
			return 0;
		}

		// Create the WIC Factory
		hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pWicFactory)
		);
		if (FAILED(hr))
		{
			ErrorLogger::Log(hr, "Failed to CoCreateInstance for WIC texture loader");
			return 0;
		}
	}

	// Load a decoder for the image
	hr = pWicFactory->CreateDecoderFromFilename(
		filename,
		NULL,
		GENERIC_READ,
		WICDecodeMetadataCacheOnLoad,
		&pWicDecoder
	);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to create decoder from file");
		return 0;
	}

	// Get image from decoder (this will decode the "frame")
	hr = pWicDecoder->GetFrame(0, &pWicFrame);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to get frame from wic decoder");
		return 0;
	}

	// Get wic pixel format of the image
	WICPixelFormatGUID pixelFormat;
	hr = pWicFrame->GetPixelFormat(&pixelFormat);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to get pixel format");
		return 0;
	}

	// Get size of the image
	UINT textureWidth, textureHieght;
	hr = pWicFrame->GetSize(&textureWidth, &textureHieght);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to Get size of texture");
		return 0;
	}

	// Convert wic pixel format to dxgi pixel format
	DXGI_FORMAT dxgiForamt = GetDXGIFormatFromWICFormat(pixelFormat);

	// If the format of the image is not a supported dxgi format, try to convert it
	if (dxgiForamt == DXGI_FORMAT_UNKNOWN)
	{
		// Get a dxgi compatable wic format from the current image format
		WICPixelFormatGUID convertToPixelFormat = GetConvertToWICFormat(pixelFormat);

		// Return if no dxgi compatable format was found
		if (convertToPixelFormat == GUID_WICPixelFormatDontCare)
		{
			ErrorLogger::Log(hr, "No dxgi compatable pixel format was found");
			return 0;
		}

		dxgiForamt = GetDXGIFormatFromWICFormat(convertToPixelFormat);

		// Create the format converter
		hr = pWicFactory->CreateFormatConverter(&pWicConverter);
		if (FAILED(hr))
		{
			ErrorLogger::Log(hr, "Failed to create format converter for wic factory");
			return 0;
		}

		// Make sure we can convert to a dxgi compatable format
		BOOL canConvert = FALSE;
		hr = pWicConverter->CanConvert(pixelFormat, convertToPixelFormat, &canConvert);
		if (FAILED(hr) || !canConvert)
		{
			ErrorLogger::Log(hr, "Cannot convert pixelFormat");
			return 0;
		}

		// Do the conversion (pWicConverter will contain converted image)
		hr = pWicConverter->Initialize(pWicFrame, convertToPixelFormat, WICBitmapDitherTypeErrorDiffusion, 0, 0, WICBitmapPaletteTypeCustom);
		if (FAILED(hr))
		{
			ErrorLogger::Log(hr, "Failed to convert frame with WIC converter");
			return 0;
		}

		// This is so we know to get the image data from the pWicConverter (otherwise we will get from pWicFrame)
		imageConverted = true;
	}

	int bitsPerPixel = GetDXGIFormatBitsPerPixel(dxgiForamt); // Number of bits per pixel
	bytesPerRow = (textureWidth * bitsPerPixel) / 8; // Number of bytes in each row of the image data
	int imageSize = bytesPerRow * textureHieght; // Total image size in bytes

	// Alocate enough memory for the raw image data, and set imageData to point to that memory
	*imageData = (BYTE*)malloc(imageSize);

	// Copy (decoded) raw image data into the newly allocated memoty (imageData)
	if (imageConverted)
	{
		// If image format needed to be comverted, the wic converter will contain the converted image
		hr = pWicConverter->CopyPixels(0, bytesPerRow, imageSize, *imageData);
		if (FAILED(hr))
		{
			ErrorLogger::Log(hr, "WIC Converter failed ot copy pixels for converted image");
			return 0;
		}
	}
	else
	{
		hr = pWicFrame->CopyPixels(0, bytesPerRow, imageSize, *imageData);
		if (FAILED(hr))
		{
			ErrorLogger::Log(hr, "Failed to copy pixels for unconverted image");
			return 0;
		}
	}

	resourceDescription = {};
	resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDescription.Alignment = 0; // May be 0, 4KB, 64KB, or 4MB. 0 will let runtime decide between 64KB and 4MB (4MB for multi-sampled textures)
	resourceDescription.Width = textureWidth; // Width of the texture
	resourceDescription.Height = textureHieght; // Height of the texture
	resourceDescription.DepthOrArraySize = 1; // If 3d image, depth of 3d image. Otherwise an array of 1d or 2d textures (We only have one image so we set to 1)
	resourceDescription.MipLevels = 1; // Number of mip maps. We are not generating mip maps for this texture, so we only have one level
	resourceDescription.Format = dxgiForamt; // This is the dxgi formatooof the image (format of the pixels)
	resourceDescription.SampleDesc.Count = 1; // This is the bymber of samples per pixel, we just want 1 sample
	resourceDescription.SampleDesc.Quality = 0; // The qulity level of the samples. Higher is better quality but worse performance
	resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // The arangmentof the pixels. Setting to unknown lets the driver choose the most efficient one
	resourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE; // No flags

	// Return the size of the image. Remembter to delete the image once youre done with it (in this tutorial once its uploaded to the GPU)
	return imageSize;
}

DXGI_FORMAT Graphics::GetDXGIFormatFromWICFormat(WICPixelFormatGUID& wicFormatGUID)
{
	if (wicFormatGUID == GUID_WICPixelFormat128bppRGBAFloat) return DXGI_FORMAT_R32G32B32A32_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBAHalf) return DXGI_FORMAT_R16G16B16A16_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBA) return DXGI_FORMAT_R16G16B16A16_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA) return DXGI_FORMAT_R8G8B8A8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGRA) return DXGI_FORMAT_B8G8R8A8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGR) return DXGI_FORMAT_B8G8R8X8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA1010102XR) return DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;

	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA1010102) return DXGI_FORMAT_R10G10B10A2_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGRA5551) return DXGI_FORMAT_B5G5R5A1_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGR565) return DXGI_FORMAT_B5G6R5_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppGrayFloat) return DXGI_FORMAT_R32_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGrayHalf) return DXGI_FORMAT_R16_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGray) return DXGI_FORMAT_R16_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppGray) return DXGI_FORMAT_R8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppAlpha) return DXGI_FORMAT_A8_UNORM;

	else return DXGI_FORMAT_UNKNOWN;
}

WICPixelFormatGUID Graphics::GetConvertToWICFormat(WICPixelFormatGUID& wicFormatGUID)
{
	if (wicFormatGUID == GUID_WICPixelFormatBlackWhite) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat1bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat2bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat4bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat2bppGray) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat4bppGray) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGrayFixedPoint) return GUID_WICPixelFormat16bppGrayHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppGrayFixedPoint) return GUID_WICPixelFormat32bppGrayFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGR555) return GUID_WICPixelFormat16bppBGRA5551;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGR101010) return GUID_WICPixelFormat32bppRGBA1010102;
	else if (wicFormatGUID == GUID_WICPixelFormat24bppBGR) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat24bppRGB) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppPBGRA) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppPRGBA) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGB) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppBGR) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppBGRA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPRGBA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPBGRA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGBFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppBGRFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBAFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppBGRAFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBHalf) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGBHalf) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppPRGBAFloat) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBFloat) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBAFixedPoint) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBFixedPoint) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBE) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppCMYK) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppCMYK) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat40bppCMYKAlpha) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat80bppCMYKAlpha) return GUID_WICPixelFormat64bppRGBA;

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8) || defined(_WIN7_PLATFORM_UPDATE)
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGB) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGB) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPRGBAHalf) return GUID_WICPixelFormat64bppRGBAHalf;
#endif

	else return GUID_WICPixelFormatDontCare;
}

int Graphics::GetDXGIFormatBitsPerPixel(DXGI_FORMAT& dxgiFormat)
{
	if (dxgiFormat == DXGI_FORMAT_R32G32B32A32_FLOAT) return 128;
	else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT) return 64;
	else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_UNORM) return 64;
	else if (dxgiFormat == DXGI_FORMAT_R8G8B8A8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B8G8R8A8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B8G8R8X8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) return 32;

	else if (dxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B5G5R5A1_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_B5G6R5_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R32_FLOAT) return 32;
	else if (dxgiFormat == DXGI_FORMAT_R16_FLOAT) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R16_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R8_UNORM) return 8;
	else if (dxgiFormat == DXGI_FORMAT_A8_UNORM) return 8;
}

int GetDXGIFormatBitsPerPixel(DXGI_FORMAT& dxgiFormat)
{
	if (dxgiFormat == DXGI_FORMAT_R32G32B32A32_FLOAT) return 128;
	else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT) return 64;
	else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_UNORM) return 64;
	else if (dxgiFormat == DXGI_FORMAT_R8G8B8A8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B8G8R8A8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B8G8R8X8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) return 32;

	else if (dxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B5G5R5A1_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_B5G6R5_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R32_FLOAT) return 32;
	else if (dxgiFormat == DXGI_FORMAT_R16_FLOAT) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R16_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R8_UNORM) return 8;
	else if (dxgiFormat == DXGI_FORMAT_A8_UNORM) return 8;
}

void Graphics::WaitForPreviousFrame()
{
	HRESULT hr;

	// Swap the current rtv buffer index so we draw on the correct buffer
	frameIndex = pSwapChain->GetCurrentBackBufferIndex();

	// If the current fence value is still less than "fenceValue", then we know the GPU has not
	// finished executing the command queuesince it has not reached the "pCommadnQueue->Signal(fence, fencevalue)"
	// command
	if (pFence[frameIndex]->GetCompletedValue() < fenceValue[frameIndex])
	{
		// We have the fence create an event which is signaled once the fence's current value is "fenceValue"
		hr = pFence[frameIndex]->SetEventOnCompletion(fenceValue[frameIndex], fenceEvent);
		if (FAILED(hr))
			Running = false;

		// We will wait until the fence had triggered the event that it's current value has reached
		// "fenceValue. Once it's value has reached "fenceValue", we know the command queue has finished
		// executing
		WaitForSingleObject(fenceEvent, INFINITE);

	}

	// Increment fenceValue for the next frame
	fenceValue[frameIndex]++;
}

void Graphics::Cleanup()
{

	// Wait for the GPU to finish all the frames
	for (int i = 0; i < frameBufferCount; i++)
	{
		frameIndex = i;
		WaitForPreviousFrame();
	}

	// Get swapchain out of full screen before exiting
	BOOL fs = false;
	if (pSwapChain->GetFullscreenState(&fs, NULL))
		pSwapChain->SetFullscreenState(false, NULL);

	pDevice.Reset();
	pSwapChain.Reset();
	pCommandQueue.Reset();
	pRtvDescriptorHeap.Reset();
	pCommandList.Reset();

	for (int i = 0; i < frameBufferCount; i++)
	{
		pRenderTargets[i].Reset();
		pCommandAllocators[i].Reset();
		pFence->Reset();
	}


}

void Graphics::Update()
{
	using namespace DirectX;
	// Create rotation matricies
	XMMATRIX rotXMat = XMMatrixRotationX(0.0001f);
	XMMATRIX rotYMat = XMMatrixRotationY(0.0002f);
	XMMATRIX rotZMat = XMMatrixRotationZ(0.0003f);

	// Add rotation to cube1's rotation matrix and store it
	XMMATRIX rotMat = XMLoadFloat4x4(&cube1RotMat) * rotXMat * rotYMat * rotZMat;
	XMStoreFloat4x4(&cube1RotMat, rotMat);

	// Create translation Matrix for cube 1 form cube1's position vector
	XMMATRIX translationMat = XMMatrixTranslationFromVector(XMLoadFloat4(&cube1Position));

	// Create cube1's world matrix by first rotationg the cube, then positioning the rotated cube
	XMMATRIX worldMat = rotMat * translationMat;

	// Store cube1's world matrix
	XMStoreFloat4x4(&cube1WorldMat, worldMat);

	// Update Constant buffer for cube1
	// create wvpMatrix and store it in constant buffer
	XMMATRIX viewMat = camera.GetViewMatrix();//XMLoadFloat4x4(&cameraViewMat); // Load view matrix
	XMMATRIX projMat = camera.GetProjectionMatrix();//XMLoadFloat4x4(&cameraProjectionMat); // load projection matrix
	XMMATRIX wvpMat = XMLoadFloat4x4(&cube1WorldMat) * viewMat * projMat; // Create wvp Matrix
	XMMATRIX transposed = XMMatrixTranspose(wvpMat); // Must transpose wvp Matrix for the GPU
	XMStoreFloat4x4(&cbPerObject.wvpMat, transposed); // Store transposed wvp Matrix in constant buffer

	// Copy our ConstantBuffe instance to the mapped constant buffer resource
	memcpy(pCbvGPUAddress[frameIndex], &cbPerObject, sizeof(cbPerObject));

	// Now do cube2's world matrix
	// Create rotation matricies for cube2
	rotXMat = XMMatrixRotationX(0.0003f);
	rotYMat = XMMatrixRotationY(0.0002f);
	rotZMat = XMMatrixRotationZ(0.0001f);

	// Add rotation to cube2's rotation matrix and store it
	rotMat = XMLoadFloat4x4(&cube2RotMat) * rotXMat * rotYMat * rotZMat;
	XMStoreFloat4x4(&cube2RotMat, rotMat);

	// Create translation Matrix for cube 1 form cube1's position vector
	XMMATRIX translationOffsetMat = XMMatrixTranslationFromVector(XMLoadFloat4(&cube2PositionOffset));

	// We want the cube to be half the size of ube 1 so we can scale it .5 in all dimensions
	XMMATRIX scaleMat = XMMatrixScaling(0.5f, 0.5f, 0.5f);

	// Reuse world mat
	// First we scalse cube2. scaling happens relative to point 0,0,0 so you will almost always want to scalse first
	// Then we translate it
	// Then we rotate it. Rotation slways rotates around 0,0,0
	// Finally we move it to cube1's position, which will cuase it to rotate around cube1
	worldMat = scaleMat * translationOffsetMat * rotMat * translationMat;

	wvpMat = XMLoadFloat4x4(&cube2WorldMat) * camera.GetViewMatrix() * camera.GetProjectionMatrix(); // Create wvp Matrix
	transposed = XMMatrixTranspose(wvpMat); // We must store wvp Matrix for GPU
	XMStoreFloat4x4(&cbPerObject.wvpMat, transposed); // Store transposed wvp Matrix in constant buffer

	// Cpoy our constant buffer instnce to the mapped constant buffer resource
	memcpy(pCbvGPUAddress[frameIndex] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject));

	// Store cube2's world Matrix
	XMStoreFloat4x4(&cube2WorldMat, worldMat);

}
