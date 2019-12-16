#include "Mesh.h"

Mesh::Mesh(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, std::vector<Vertex3D>& verticies, std::vector<DWORD>& indicies, std::vector<Texture>& textures, const DirectX::XMMATRIX& transformMatrix)
{
	m_commandlist = commandList;
	m_textures = textures;
	m_transformMatrix = transformMatrix;

	HRESULT hr = this->m_vertexBuffer.Initialize(device, verticies.data(), (UINT)verticies.size());
	COM_ERROR_IF_FAILED(hr, "Failed to initialize vertex buffer for mesh");
}

Mesh::Mesh(const Mesh& mesh)
{
	this->m_commandlist = mesh.m_commandlist;
	this->m_indexBuffer = mesh.m_indexBuffer;
	this->m_vertexBuffer = mesh.m_vertexBuffer;
	this->m_textures = mesh.m_textures;
	this->m_transformMatrix = mesh.m_transformMatrix;
}

void Mesh::Draw()
{
	m_commandlist->DrawIndexedInstanced(m_indexBuffer.IndexCount(), 1, 0, 0, 0);
}

const DirectX::XMMATRIX& Mesh::GetTransformMatrix()
{
	return this->m_transformMatrix;
}
