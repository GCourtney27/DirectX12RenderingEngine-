#pragma once
#include "ErrorLogger.h"

class WindowContainer;

class RenderWindow
{
public:
	bool Initialize(WindowContainer* pWindowContainer, HINSTANCE hInstance, int ShowWnd, LPCTSTR window_title, LPCTSTR window_name, int width, int height, bool fullscreen);
	bool ProccessMessages();
	HWND GetHWND();
	~RenderWindow();
private:
	void RegisterWindowClass();
	HWND hwnd = NULL; // Handle to this window
	HINSTANCE hInstance = NULL; // Handle to the application Instance

	//std::string window_title = "";
	//std::wstring window_title_wide = L"";
	//std::string window_class = "";

	LPCTSTR WindowName = L"";
	LPCTSTR WindowTitle = L"";

	std::wstring window_class_wide = L"";
	int width;
	int height;
	bool fullscreen;
};