#pragma once
#include "AdapterReader.h"
#include <D3Dcompiler.h>
#include "../d3dx12.h"
#include "Vertex.h"
#include "ConstantBufferPerObject.h"
#include <DirectXMath.h>
#include <wincodec.h>

#include "Objects/Camera3D.h"
#include "RenderableGameObject.h"

#include <dxcapi.h>
#include <vector>
#include "DXRHelpers/nv_helpers_dx12/TopLevelASGenerator.h"
#include "DXRHelpers/nv_helpers_dx12/ShaderBindingTableGenerator.h"

#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }
using Microsoft::WRL::ComPtr;

class Graphics
{	
public:
	bool Initialize(HWND hwnd, int width, int height);
	void RenderFrame();
	void WaitForPreviousFrame();
	void Cleanup();

	void Update();

	void SetRasterEnabled(bool enabled) { m_raster = enabled; }
	bool GetIsRasterEnabled() { return m_raster; }

	Camera3D camera;

private:
	bool InitializeDirect3D12(HWND hwnd);
	void UpdatePipeline();
	bool InitializeShaders();
	bool InitializeScene();
	void UpdateImGui();

#pragma region Ray Tracing
	bool m_raster = true;
	struct AccelerationStructureBuffers
	{
		ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder
		ComPtr<ID3D12Resource> pResult;       // Where the AS is
		ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};
	ComPtr<ID3D12Resource> m_bottomLevelAS; // Storage for the bottom Level AS

	nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator;
	AccelerationStructureBuffers m_topLevelASBuffers;
	std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances;

	void CheckRayTracingSupport();
	AccelerationStructureBuffers CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers);
	void CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances);
	void CreateAccelerationStructures();
	ComPtr<ID3D12RootSignature> CreateRayGenSignature();
	ComPtr<ID3D12RootSignature> CreateMissSignature();
	ComPtr<ID3D12RootSignature> CreateHitSignature();
	void CreateRaytracingPipeline();
	void CreateRaytracingOutputBuffer();
	void CreateShaderResourceHeap();
	void CreateShaderBindingTable();

	nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
	ComPtr<ID3D12Resource> m_sbtStorage;
	ComPtr<ID3D12Resource> m_outputResource;
	ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
	ComPtr<IDxcBlob> m_rayGenLibrary;
	ComPtr<IDxcBlob> m_hitLibrary;
	ComPtr<IDxcBlob> m_missLibrary;

	ComPtr<ID3D12RootSignature> m_rayGenSignature;
	ComPtr<ID3D12RootSignature> m_hitSignature;
	ComPtr<ID3D12RootSignature> m_missSignature;

	// Ray tracing pipeline state
	ComPtr<ID3D12StateObject> m_rtStateObject;
	// Ray tracing pipeline state properties, retaining the shader identifiers
	// to use in the Shader Binding Table
	ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;

#pragma endregion Ray Tracing

	// D3D declarations
	const static int frameBufferCount = 3; // Number of buffers we want
	ComPtr<ID3D12Device5> pDevice; // d3d device
	ComPtr<IDXGISwapChain3> pSwapChain; // Swapchain used to switch between render targets
	ComPtr<ID3D12CommandQueue> pCommandQueue; // Container for command list
	ComPtr<ID3D12DescriptorHeap> pRtvDescriptorHeap; // A descriptor heap to hold resources like the render targets
	ComPtr<ID3D12Resource> pRenderTargets[frameBufferCount]; // Number of render targets equal to buffer count
	ComPtr<ID3D12CommandAllocator> pCommandAllocators[frameBufferCount]; // We want enough allocators for each buffer * number of thread (We only have 1 thread to frameBuffer Count)
	ComPtr<ID3D12GraphicsCommandList4> pCommandList; // Acoomand list we can record commands into, then execute them to render the frame
	ComPtr<ID3D12Fence> pFence[frameBufferCount]; // An object that is locked while our command list is being executed by the GPU. We need as many
																  // as we have allocators (more if we want to know when the gpu is finished with an asset)
	ComPtr<ID3D12PipelineState> pPipelineStateObject; // PSO containg a pipeline state
	ComPtr<ID3D12RootSignature> pRootSignature; // Root signature defines data shaders will access
	ComPtr<ID3D12Resource> pIndexBuffer; // A default buffer in GPU memory that we will load index data for our triangle into
	D3D12_INDEX_BUFFER_VIEW indexBufferView; // A structure holding information about the index buffer
	
	ComPtr<ID3D12Resource> pDepthStencilBuffer; // This is the memory for out depth buffer. It will also be used tor stencil buffer
	ComPtr<ID3D12DescriptorHeap> pDSDescriptorHeap; // This is a heap for oue depth/stencil buffer descriptor
	
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pMainDescriptorHeap[frameBufferCount]; // This heap will store the descriptor to our contant buffer
	
	int ConstantBufferPerObjectAlignedSize = (sizeof(ConstantBufferPerObject) + 255) & ~255;
	ConstantBufferPerObject cbPerObject; // This is the constant buffer data we will send to the GPU
											// (Which will be placed in the resource we created above)
	ComPtr<ID3D12Resource> pConstantBufferUploadHeaps[frameBufferCount]; // This is the memory on the gpu where our contant buffer will be placed
	UINT8* pCbvGPUAddress[frameBufferCount]; // This is a pointer to each of the constant buffer resource heaps

	RenderableGameObject cube;

	// -- Move these to game object class -- //
	DirectX::XMFLOAT4X4 cube1WorldMat; // our first cub's world Matrix (Transformation Matrix)
	DirectX::XMFLOAT4X4 cube1RotMat; // This will keep track of our rotation for the first cube
	DirectX::XMFLOAT4 cube1Position; // Our first cubes position in space

	DirectX::XMFLOAT4X4 cube2WorldMat; // Our first cubes World Matrix (Transfomation Matrix)
	DirectX::XMFLOAT4X4 cube2RotMat; // This will keep track of our rottion for our second cube
	DirectX::XMFLOAT4 cube2PositionOffset; // Our second cube will rotate around our first cube, so this is the position offset from the first cube


	Microsoft::WRL::ComPtr<ID3D12Resource> pTextureBuffer; // The resource heap containing our texture
	int LoadImageDataFromFile(BYTE** imageData, D3D12_RESOURCE_DESC& resourceDescription, LPCWSTR filename, int &bytesPerRow);

	DXGI_FORMAT GetDXGIFormatFromWICFormat(WICPixelFormatGUID& wicFormatGUID);
	WICPixelFormatGUID GetConvertToWICFormat(WICPixelFormatGUID& wicFormatGUID);
	int GetDXGIFormatBitsPerPixel(DXGI_FORMAT& dxgiFormat);

	//ID3D12DescriptorHeap* pMainDescriptorHeap;
	ID3D12Resource* pTextureBufferUploadHeap;

	ConstantBuffer<ConstantBufferPerObject> cb_vertexShader;

	int numCubeIndices; // The number of indices to draw the cube

	D3D12_VIEWPORT viewPort; // Area that the output from the rasterizer will be stretched to
	D3D12_RECT scissorRect; // The area to draw in. Pixels outside that area will not be drawn
	ComPtr<ID3D12Resource> pVertexBuffer; // A default buffer in GPU memory that we will load vertex data for out triangles into
	D3D12_VERTEX_BUFFER_VIEW vertexbufferView; // A structure containing a pointe to the vetex data in GPU memory
												// The total size of the buffer, and the size of each element (vertex)
public:
	HANDLE fenceEvent; // An handle to an event when our fence is unlocked by the GPU
private:
	UINT64 fenceValue[frameBufferCount]; // This value is incremented each frame. Each fence will have its own value
	int frameIndex; // Current RTV we are on
	int rtvDescriptorSize; // Size of the RTV descriptor on the device (all front to back buffers will be the same size)


	int windowWidth;
	int windowHeight;
	bool Running = false;
};