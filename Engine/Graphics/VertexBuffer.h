#ifndef VertexBuffer_h__
#define VertexBuffer_h__
#include <../d3dx12.h>
#include <wrl/client.h>
#include <memory>

template<class T>
class VertexBuffer
{
private:
	Microsoft::WRL::ComPtr <ID3D12Resource> pVertexBuffer; // ID3D12Resource equivelent to ID3D11Buffer
	UINT stride = sizeof(T);
	UINT vertexCount = 0;

public:
	VertexBuffer() {}

	VertexBuffer(const VertexBuffer<T>& rhs)
	{
		this->pVertexBuffer = rhs.pVertexBuffer;
		this->vertexCount = rhs.vertexCount;
		this->stride = rhs.stride;
	}

	VertexBuffer<T>& operator =(const VertexBuffer<T>& a)
	{
		this->pVertexBuffer = a.pVertexBuffer;
		this->vertexCount = a.vertexCount;
		this->stride = a.stride;
		return *this;
	}

	ID3D12Resource* Get()const
	{
		return pVertexBuffer.Get();
	}

	ID3D12Resource* const* GetAddressOf()const
	{
		return pVertexBuffer.GetAddressOf();
	}

	UINT VertexCount() const
	{
		return this->vertexCount;
	}

	const UINT Stride() const
	{
		return this->stride;
	}

	const UINT* StridePtr() const
	{
		return &this->stride;
	}

	HRESULT Initialize(ID3D12Device* device, T* data, UINT vertexCount)
	{
		if (pVertexBuffer.Get() != nullptr)
		{
			pVertexBuffer.Reset();
		}
		this->vertexCount = vertexCount / sizeof(UINT);

		HRESULT hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(stride *vertexCount),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&pVertexBuffer)
		);
		pVertexBuffer->SetName(L"Vertex Buffer Resource Heap");
		return hr;
	}
};
#endif // VertexBuffer_h__