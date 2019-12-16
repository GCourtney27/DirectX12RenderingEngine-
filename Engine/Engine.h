#pragma once
#include "WindowContainer.h"
#include "Timer.h"
//#include "FileLoader.h"
//#include "Graphics/Ray.h"
//#include "Graphics/DirectX_Include.h"
//#include "Graphics/DebugModels.h"

class Engine : WindowContainer
{
public:
	Engine() {}
	~Engine() {}
	bool Initialize(HINSTANCE hInstance, LPCTSTR window_title, LPCTSTR window_name, int nCmdShow, int width, int height);
	bool ProccessMessages();
	void Update();
	void RenderFrame();
	bool SaveScene();

	void Shutdown();

	//bool hit_sphere(const SimpleMath::Vector3& center, float radius, const Ray& r);
	//float intersection_distance(const SimpleMath::Vector3& center, float radius, const Ray& r);

	//DirectX::XMFLOAT3 GetMouseDirectionVector();

private:
	Timer timer;
	int windowWidth = 0;
	int windowHeight = 0;

};