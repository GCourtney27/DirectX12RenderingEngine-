#include "AdapterReader.h"

std::vector<AdapterData> AdapterReader::m_adapters;

AdapterData::AdapterData(IDXGIAdapter1* pAdapter)
{
	this->pAdapter = pAdapter;
	HRESULT hr = pAdapter->GetDesc1(&this->description);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to Get Description fo IDXGIAdapter");
	}
}

std::vector<AdapterData> AdapterReader::GetAdapters()
{
	if (m_adapters.size() > 0)
	{
		return m_adapters;
	}
	Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	COM_ERROR_IF_FAILED(hr, "Failed to create dxgi factory.");

	IDXGIAdapter1* pAdapter;// Adapters are the graphics card (This includes the embeded graphics on the motherboard)

	UINT adapterIndex = 0;// We'll start looking for DirectX 12 compatible graphics devices starting at index 0

	bool adapterFound = false;

	// Find the first hardawre GPU the supports d3d 12
	while ((dxgiFactory->EnumAdapters1(adapterIndex, &pAdapter) != DXGI_ERROR_NOT_FOUND))
	{
		DXGI_ADAPTER_DESC1 desc;
		pAdapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			adapterIndex++;
			continue;
		}

		// We want a device that is compatible with Direct3D 12 (feature level 11 or higher)
		hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr);
		if (SUCCEEDED(hr))
		{
			adapterFound = true;
			m_adapters.push_back(AdapterData(pAdapter));
		}

		adapterIndex++;
	}
	return m_adapters;
}
