#include "stdafx.h"

#include "Engine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN    // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h"
#include <string>
#include <wrl/client.h>

#pragma comment (lib, "d3d12.lib")

#pragma comment(lib, "dxgi.lib")

int WINAPI wWinMain(
	 HINSTANCE hInstance,
	 HINSTANCE pInstance,
	 PWSTR pCmdLine,
	 int nCmdShow)
{	

	HRESULT hr = CoInitialize(NULL);
	if (FAILED(hr))
	{
		ErrorLogger::Log(hr, "Failed to CoInitialize");
	}

	Engine engine;
	if (engine.Initialize(hInstance, L"DX12 Engine", L"Hello World!", nCmdShow, 1600, 900))
	{
		while (engine.ProccessMessages() == true)
		{
			engine.Update();
			engine.RenderFrame();
		}
	}
	engine.Shutdown();

	
	return 0;
}
