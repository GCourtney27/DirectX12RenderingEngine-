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

// this will only call release if an object exists (prevents exceptions calling release on non existant objects)
#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }

class Graphics
{
public:
	bool Initialize(HWND hwnd, int width, int height);
	void RenderFrame();
	void WaitForPreviousFrame();
	void Cleanup();

	void Update();

	Camera3D camera;

private:
	bool InitializeDirect3D12(HWND hwnd);
	void UpdatePipeline();
	bool InitializeShaders();
	bool InitializeScene();
	void UpdateImGui();

	// D3D declarations
	const static int frameBufferCount = 3; // Number of buffers we want
	Microsoft::WRL::ComPtr<ID3D12Device> pDevice; // d3d device
	Microsoft::WRL::ComPtr<IDXGISwapChain3> pSwapChain; // Swapchain used to switch between render targets
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> pCommandQueue; // Container for command list
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pRtvDescriptorHeap; // A descriptor heap to hold resources like the render targets
	Microsoft::WRL::ComPtr<ID3D12Resource> pRenderTargets[frameBufferCount]; // Number of render targets equal to buffer count
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> pCommandAllocators[frameBufferCount]; // We want enough allocators for each buffer * number of thread (We only have 1 thread to frameBuffer Count)
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pCommandList; // Acoomand list we can record commands into, then execute them to render the frame
	Microsoft::WRL::ComPtr<ID3D12Fence> pFence[frameBufferCount]; // An object that is locked while our command list is being executed by the GPU. We need as many
																  // as we have allocators (more if we want to know when the gpu is finished with an asset)
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pPipelineStateObject; // PSO containg a pipeline state
	Microsoft::WRL::ComPtr<ID3D12RootSignature> pRootSignature; // Root signature defines data shaders will access
	Microsoft::WRL::ComPtr<ID3D12Resource> pIndexBuffer; // A default buffer in GPU memory that we will load index data for our triangle into
	D3D12_INDEX_BUFFER_VIEW indexBufferView; // A structure holding information about the index buffer
	
	Microsoft::WRL::ComPtr<ID3D12Resource> pDepthStencilBuffer; // This is the memory for out depth buffer. It will also be used tor stencil buffer
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pDSDescriptorHeap; // This is a heap for oue depth/stencil buffer descriptor
	
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pMainDescriptorHeap[frameBufferCount]; // This heap will store the descriptor to our contant buffer
	
	int ConstantBufferPerObjectAlignedSize = (sizeof(ConstantBufferPerObject) + 255) & ~255;
	ConstantBufferPerObject cbPerObject; // This is the constant buffer data we will send to the GPU
											// (Which will be placed in the resource we created above)
	Microsoft::WRL::ComPtr<ID3D12Resource> pConstantBufferUploadHeaps[frameBufferCount]; // This is the memory on the gpu where our contant buffer will be placed
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
	Microsoft::WRL::ComPtr<ID3D12Resource> pVertexBuffer; // A default buffer in GPU memory that we will load vertex data for out triangles into
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