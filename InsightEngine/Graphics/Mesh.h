#pragma once
#include "Vertex.h"
#include "ConstantBufferPerObject.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Texture.h" // <---- TODO: Implement texture class!!
#include "ConstantBuffers.h" // <---- TODO: Create ContantBuffer class!!

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

class Mesh
{
public:
	Mesh(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, std::vector<Vertex3D>& verticies, std::vector<DWORD>& indicies, std::vector<Texture>& textures, const DirectX::XMMATRIX& transformMatrix);
	Mesh(const Mesh& mesh);
	void Draw();
	const DirectX::XMMATRIX& GetTransformMatrix();

private:
	VertexBuffer<Vertex3D> m_vertexBuffer; // A mesh can have a bunch of verticies
	IndexBuffer m_indexBuffer; // Mesh can have a bunch of Indicies
	ID3D12GraphicsCommandList* m_commandlist;
	std::vector<Texture> m_textures;
	DirectX::XMMATRIX m_transformMatrix;
};