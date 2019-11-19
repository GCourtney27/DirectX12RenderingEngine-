#include "WindowContainer.h"

bool RenderWindow::Initialize(WindowContainer* pWindowContainer, HINSTANCE hInstance, int ShowWnd, LPCTSTR window_title, LPCTSTR window_name, int width, int height, bool isFullscreen)
{

	this->hInstance = hInstance;
	this->width = width;
	this->height = height;
	this->WindowName = window_name;
	this->WindowTitle = window_title;
	//this->window_title = window_title;
	//this->window_title_wide = StringHelper::StringToWide(this->window_title);
	//this->window_class = window_class;
	//this->window_class_wide = StringHelper::StringToWide(this->window_title);

	this->RegisterWindowClass();
	int centerScreenX = GetSystemMetrics(SM_CXSCREEN) / 2 - this->width / 2;
	int centerScreenY = GetSystemMetrics(SM_CYSCREEN) / 2 - this->height/ 2;

	if (fullscreen)
	{
		HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(hmon, &mi);

		width = mi.rcMonitor.right - mi.rcMonitor.left;
		height = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}

	RECT wr; // Window Rectangle
	wr.left = centerScreenX;
	wr.right = centerScreenY;
	wr.top = wr.left + this->width;
	wr.bottom = wr.top + this->height;

	AdjustWindowRect(&wr, WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU, FALSE);

	hwnd = CreateWindowEx(NULL,
							WindowName,
							WindowTitle,
							WS_OVERLAPPEDWINDOW,
							CW_USEDEFAULT, CW_USEDEFAULT,
							width, height,
							NULL,
							NULL,
							hInstance,
							pWindowContainer);

	if (this->hwnd == NULL)
	{
		ErrorLogger::Log(GetLastError(), "Failed to CraeteWindowEx for window");
		return false;
	}

	if (isFullscreen)
		SetWindowLong(hwnd, GWL_STYLE, 0);

	ShowWindow(hwnd, ShowWnd);
	SetForegroundWindow(this->hwnd);
	SetFocus(this->hwnd);
	UpdateWindow(hwnd);

	return true;
}

LRESULT CALLBACK HandleMsgRedirect(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		// All other messages
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;

	default:
		{
			// Retrieve ptr to window class
			WindowContainer* const pWindow = reinterpret_cast<WindowContainer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
			// Foreward message to window class handler
			return pWindow->WindowProc(hwnd, uMsg, wParam, lParam);
		}
	}
}

bool RenderWindow::ProccessMessages()
{

	MSG msg;
	ZeroMemory(&msg, sizeof(MSG));

	while (PeekMessage(&msg,  // Where to store message (ifone exists)
		this->hwnd, //Handle to window we are checking messages for
		0, // Minimum Filter Msg Value - We are not filterinf for specific messages but min and max could be used to do so
		0, // Maximum Filter Msg Value
		PM_REMOVE)) // Remove mesage after captureing it via PeekMessage
	{
		if (msg.message == WM_QUIT)
			return false;
		TranslateMessage(&msg);  // Translate message from virtual key message into character messages
		DispatchMessage(&msg); // Dispatch message to our Window Proc for this window
	}

	// Check if the window was closed with the top right X button
	if (msg.message == WM_NULL)
	{
		if (!IsWindow(this->hwnd))
		{
			this->hwnd = NULL; // Message proccessing loop takes care of destroying this window
			UnregisterClass(this->window_class_wide.c_str(), this->hInstance);
			return false;
		}
	}
#ifdef _DEBUG
	assert(_CrtCheckMemory()); // Make sure the heap isn's corrupted on exit
#endif

	return true;
}

HWND RenderWindow::GetHWND()
{
	return this->hwnd;
}

RenderWindow::~RenderWindow()
{
	if (this->hwnd != NULL)
	{
		UnregisterClass(this->window_class_wide.c_str(), this->hInstance);
		DestroyWindow(hwnd);
	}
}

LRESULT CALLBACK HandleMessageSetup(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_NCCREATE:
	{
		const CREATESTRUCTW* const pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
		WindowContainer* pWindow = reinterpret_cast<WindowContainer*>(pCreate->lpCreateParams);
		if (pWindow == nullptr)
		{
			ErrorLogger::Log("Critical Error: Pointer to window container is null durring WM_NCCREATE.");
			exit(-1);
		}
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
		SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HandleMsgRedirect));
		return pWindow->WindowProc(hwnd, uMsg, wParam, lParam);
	}
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}

void RenderWindow::RegisterWindowClass()
{
	WNDCLASSEX wc;

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = HandleMessageSetup;
	wc.cbClsExtra = NULL;
	wc.cbWndExtra = NULL;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = WindowName;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	if (!RegisterClassEx(&wc))
	{
		MessageBox(NULL, L"Error register class", L"Error", MB_OK | MB_ICONERROR);
	}
}
