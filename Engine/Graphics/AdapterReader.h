#pragma once
#include "../ErrorLogger.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>

#pragma comment(lib, "dxgi.lib")

class AdapterData
{
public:
	AdapterData(IDXGIAdapter1* pAdapter);
	IDXGIAdapter1* pAdapter = nullptr;
	DXGI_ADAPTER_DESC1 description;
};

class AdapterReader
{
public: 
	static std::vector<AdapterData> GetAdapters();
private:
	static std::vector<AdapterData> m_adapters;
};