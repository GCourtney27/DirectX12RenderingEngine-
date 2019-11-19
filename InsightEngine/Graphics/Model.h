#pragma once
#include "Mesh.h"

using namespace DirectX;

class Model
{
public:
	bool Initialize(const std::string& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* deviceContext, ConstantBuffer<ConstantBufferPerObject>& cb_vs_vertexshader);
	void Draw(const XMMATRIX& worldMatrix, const XMMATRIX& viewProjectionMatrix, int rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress);

private:
	std::vector<Mesh> meshes;
	bool LoadModel(const std::string& filepath);
	void ProcessNode(aiNode* node, const aiScene* scene, const XMMATRIX& parentTransformMatrix);
	Mesh ProcessMesh(aiMesh* mesh, const aiScene* scene, const XMMATRIX& transformMatrix);
	TextureStorageType DetermineTextureStorageType(const aiScene* pScene, aiMaterial* pMat, unsigned int index, aiTextureType textureType);
	std::vector<Texture> LoadMaterialTextures(aiMaterial* pMaterial, aiTextureType textureType, const aiScene* pScene);
	int GetTextureIndex(aiString* pStr);

	ID3D12Device* device = nullptr;
	ID3D12GraphicsCommandList* commandList = nullptr;
	ConstantBuffer<ConstantBufferPerObject>* cb_vs_vertexshader = nullptr;
	std::string directory = "";
};