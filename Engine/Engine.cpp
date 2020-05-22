#include "Engine.h"

bool Engine::Initialize(HINSTANCE hInstance, LPCTSTR window_title, LPCTSTR window_name, int nCmdShow, int width, int height)
{
	windowHeight = height;
	windowWidth = windowWidth;
	
	timer.Start();
	if (!render_window.Initialize(this, hInstance, nCmdShow, window_title, window_name, width, height, false))
		return false;

	if (!gfx.Initialize(this->render_window.GetHWND(), width, height))
		return false;

	return true;
}

bool Engine::ProccessMessages()
{
	return this->render_window.ProccessMessages();
}

void Engine::Update()
{
	float deltaTime = (float)timer.GetMilisecondsElapsed();
	timer.Restart();

	gfx.Update();
	
	while (!keyboard.CharBufferIsEmpty())
	{
		unsigned char ch = keyboard.ReadChar();
	}

	while (!keyboard.KeyBufferIsEmpty())
	{
		KeyboardEvent kbe = keyboard.ReadKey();
		unsigned char keycode = kbe.GetKeyCode();
	}

	while (!mouse.EventBufferIsEmpty())
	{
		MouseEvent me = mouse.ReadEvent();
		if (mouse.IsRightDown())
		{
			if (me.GetType() == MouseEvent::EventType::RAW_MOVE)
			{
				this->gfx.camera.AdjustRotation((float)me.GetPosY() * 0.01f, (float)me.GetPosX() * 0.01f, 0);
			}
		}
	}

	float cameraSpeed = 0.01f;
	if (keyboard.KeyIsPressed(VK_SHIFT))
	{
		cameraSpeed = 0.1f;
	}

	m_ChangeRenderPathDelay -= 0.01 * deltaTime;
	if (m_ChangeRenderPathDelay <= 0.0f)
		m_CanChangeRenderPath = true;
	if (keyboard.KeyIsPressed(' ') && m_CanChangeRenderPath)
	{
		m_CanChangeRenderPath = false;
		m_ChangeRenderPathDelay = 3.0f;
		this->gfx.SetRasterEnabled(!this->gfx.GetIsRasterEnabled());
	}

	if (keyboard.KeyIsPressed('W'))
	{
		this->gfx.camera.AdjustPosition(this->gfx.camera.GetForwardVector() * cameraSpeed * deltaTime);
	}
	if (keyboard.KeyIsPressed('S'))
	{
		this->gfx.camera.AdjustPosition(this->gfx.camera.GetBackwardVector() * cameraSpeed * deltaTime);
	}
	if (keyboard.KeyIsPressed('A'))
	{
		this->gfx.camera.AdjustPosition(this->gfx.camera.GetLeftVector() * cameraSpeed * deltaTime);
	}
	if (keyboard.KeyIsPressed('D'))
	{
		this->gfx.camera.AdjustPosition(this->gfx.camera.GetRightVector() * cameraSpeed * deltaTime);
	}
	if (keyboard.KeyIsPressed('E'))
	{
		this->gfx.camera.AdjustPosition(0.0f, cameraSpeed * deltaTime, 0.0f);
	}
	if (keyboard.KeyIsPressed('Q'))
	{
		this->gfx.camera.AdjustPosition(0.0f, -cameraSpeed * deltaTime, 0.0f);
	}
	if (keyboard.KeyIsPressed(27))
	{
		exit(0); // Performs no cleanup
		//PostMessage(this->render_window.GetHWND(), WM_QUIT, 0, 0);
	}

	if (keyboard.KeyIsPressed('C'))
	{
		//lightPosition += this->gfx.camera.GetForwardVector();
		//this->gfx.light.SetPosition(this->gfx.camera.GetPositionFloat3());
		//this->gfx.light.SetRotation(this->gfx.camera.GetRotationFloat3());
	}

}

void Engine::RenderFrame()
{
	gfx.RenderFrame();
}

void Engine::Shutdown()
{
	gfx.WaitForPreviousFrame();
	CloseHandle(gfx.fenceEvent);
	gfx.Cleanup();

}
