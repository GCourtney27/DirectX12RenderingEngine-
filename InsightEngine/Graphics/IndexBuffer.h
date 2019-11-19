#ifndef IndicesBuffer_h__
#define IndicesBuffer_h__
#include <../d3dx12.h>
#include <wrl/client.h>
#include <vector>

class IndexBuffer
{
private:
	IndexBuffer(const IndexBuffer& rhs);

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> pIndexBuffer;
	UINT indexCount = 0;
public:
	IndexBuffer() {}

	ID3D12Resource* Get()const
	{
		return pIndexBuffer.Get();
	}

	ID3D12Resource* const* GetAddressOf()const
	{
		return pIndexBuffer.GetAddressOf();
	}

	UINT IndexCount() const
	{
		return this->indexCount;
	}

	HRESULT Initialize(ID3D12Device* device, DWORD* data, UINT indexCount)
	{
		if (pIndexBuffer.Get() != nullptr)
			pIndexBuffer.Reset();

		this->indexCount = indexCount / sizeof(UINT);
		//Load Index Data
		HRESULT hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(indexCount),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&pIndexBuffer)
		);
		pIndexBuffer->SetName(L"Vertex Buffer Resource Heap");
		return hr;
	}
};

#endif // IndicesBuffer_h__