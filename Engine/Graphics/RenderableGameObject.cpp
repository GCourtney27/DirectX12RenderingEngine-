#include "RenderableGameObject.h"

bool RenderableGameObject::Initialize(const std::string& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* deviceContext, ConstantBuffer<ConstantBufferPerObject>& cb_vs_vertexshader)
{
	if (!model.Initialize(filepath, device, deviceContext, cb_vs_vertexshader))
		return false;

	this->SetPosition(0.0f, 0.0f, 0.0f);
	this->SetRotation(0.0f, 0.0f, 0.0f);
	this->UpdateMatrix();

	//sphere_radius = 20.0f;
	//sphere_position = GetPositionFloat3();


	return true;
}

void RenderableGameObject::Draw(const XMMATRIX& viewProjectionMatrix, int rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
{
	model.Draw(this->worldMatrix, viewProjectionMatrix, rootParameterIndex, gpuAddress);
	AdjustPosition(0.0f, 0.0f, 0.0f);
	// TODO: Update sphere collider (Move this somewhere else)
	sphere_position = GetPositionFloat3();
}


void RenderableGameObject::UpdateMatrix()
{
	this->worldMatrix = XMMatrixScaling(this->scale.x, this->scale.y, this->scale.z) * XMMatrixRotationRollPitchYaw(this->rot.x, this->rot.y, this->rot.z) * XMMatrixTranslation(this->pos.x, this->pos.y, this->pos.z);
	this->UpdateDirectionVectors();
}