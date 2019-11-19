#pragma once
#include <DirectXMath.h>

struct Vertex3D 
{
	Vertex3D() {}
	Vertex3D(float x, float y, float z, float r, float u, float v) : pos(x, y, z), textCoord(u, v) {}

	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT2 textCoord;
	//DirectX::XMFLOAT3 normal;
};