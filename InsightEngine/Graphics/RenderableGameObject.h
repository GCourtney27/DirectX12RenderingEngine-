#pragma once
#include "GameObject3D.h"
#include <SimpleMath.h>

class RenderableGameObject : public GameObject3D
{
public:
	RenderableGameObject() {}
	bool Initialize(const std::string& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* deviceContext, ConstantBuffer<ConstantBufferPerObject>& cb_vs_vertexshader); //float boundingSphere scale
	void Draw(const XMMATRIX& viewProjectionMatrix, int rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);

	SimpleMath::Vector3 sphere_position;
	float sphere_radius = 0.0f;

protected:
	Model model;
	void UpdateMatrix() override;

	XMMATRIX worldMatrix = XMMatrixIdentity();

};