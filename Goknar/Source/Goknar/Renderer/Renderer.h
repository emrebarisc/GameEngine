#ifndef __RENDERER_H__
#define __RENDERER_H__

#include "Goknar/Core.h"
#include "Goknar/Renderer/Types.h"

#include "Goknar/Model/MeshUnit.h"

#include "glad/glad.h"

#include <vector>

class DynamicMesh;
class StaticMesh;
class SkeletalMesh;
class ShadowManager;

class Texture;
class Framebuffer;
class Shader;

class DynamicMeshInstance;
class StaticMeshInstance;
class SkeletalMeshInstance;

enum class GOKNAR_API RenderPassType : unsigned int
{
	None = 0b00000000,
	Forward = 0b00000001,
	Shadow = 0b00000010,

	// Needed for rendering point lights with geometry shaders
	PointLightShadow,
	GeometryBuffer = 0b00000100,
	Deferred = 0b00001000
};

class GOKNAR_API GeometryBufferData
{
public:
	GeometryBufferData();
	~GeometryBufferData();

	void Init();
	void Bind();
	void Unbind();

	Framebuffer* geometryFrameBuffer;

	Texture* worldPositionTexture;
	Texture* worldNormalTexture;
	Texture* diffuseTexture;
	Texture* specularTexture;

	unsigned int depthRenderbuffer{ 0 };
private:
};

class GOKNAR_API DeferredRenderingData
{
public:
	DeferredRenderingData();
	~DeferredRenderingData();

	void Init();
	void BindGeometryBuffer();
	void UnbindGeometryBuffer();

	void Render();

	void BindGeometryBufferTextures(Shader* shader);

	GeometryBufferData* geometryBufferData{ nullptr };
	StaticMesh* deferredRenderingMesh{ nullptr };
	Shader* deferredRenderingMeshShader{ nullptr };
private:
};

class GOKNAR_API Renderer
{
	friend DeferredRenderingData;

public:
	Renderer();
	~Renderer();

	void SetStaticBufferData();
	void SetSkeletalBufferData();
	void SetDynamicBufferData();
	void SetBufferData();

	void PreInit();
	void Init();
	void PostInit();

	void Render(RenderPassType renderPassType);

	void AddStaticMeshToRenderer(StaticMesh* object);
	void AddStaticMeshInstance(StaticMeshInstance* object);
	void RemoveStaticMeshInstance(StaticMeshInstance* object);

	void AddSkeletalMeshToRenderer(SkeletalMesh* object);
	void AddSkeletalMeshInstance(SkeletalMeshInstance* object);
	void RemoveSkeletalMeshInstance(SkeletalMeshInstance* object);

	void AddDynamicMeshToRenderer(DynamicMesh* object);
	void AddDynamicMeshInstance(DynamicMeshInstance* object);
	void RemoveDynamicMeshInstance(DynamicMeshInstance* object);

	void UpdateDynamicMeshVertex(const DynamicMesh* object, int vertexIndex, const VertexData& newVertexData);

	ShadowManager* GetShadowManager()
	{
		return shadowManager_;
	}

	void BindShadowTextures(Shader* shader);
	void BindGeometryBufferTextures(Shader* shader);
	void SetLightUniforms(Shader* shader);

	void SetMainRenderType(RenderPassType type)
	{
		mainRenderType_ = type;
	}

	RenderPassType GetMainRenderType() const
	{
		return mainRenderType_;
	}

private:
	void BindStaticVBO();
	void BindSkeletalVBO();
	void BindDynamicVBO();
	void SetAttribPointers();
	void SetAttribPointersForSkeletalMesh();

	std::vector<StaticMesh*> staticMeshes_;
	std::vector<SkeletalMesh*> skeletalMeshes_;
	std::vector<DynamicMesh*> dynamicMeshes_;

	std::vector<StaticMeshInstance*> opaqueStaticMeshInstances_;
	//TODO: Is it really necessary to hold masked objects as a seperate array?
	std::vector<StaticMeshInstance*> maskedStaticMeshInstances_;
	std::vector<StaticMeshInstance*> translucentStaticMeshInstances_;

	std::vector<SkeletalMeshInstance*> opaqueSkeletalMeshInstances_;
	//TODO: Is it really necessary to hold masked objects as a seperate array?
	std::vector<SkeletalMeshInstance*> maskedSkeletalMeshInstances_;
	std::vector<SkeletalMeshInstance*> translucentSkeletalMeshInstances_;

	std::vector<DynamicMeshInstance*> opaqueDynamicMeshInstances_;
	//TODO: Is it really necessary to hold masked objects as a seperate array?
	std::vector<DynamicMeshInstance*> maskedDynamicMeshInstances_;
	std::vector<DynamicMeshInstance*> translucentDynamicMeshInstances_;

	ShadowManager* shadowManager_{ nullptr };

	DeferredRenderingData* deferredRenderingData_{ nullptr };

	unsigned int totalStaticMeshVertexSize_;
	unsigned int totalStaticMeshFaceSize_;

	unsigned int totalSkeletalMeshVertexSize_;
	unsigned int totalSkeletalMeshFaceSize_;

	unsigned int totalDynamicMeshVertexSize_;
	unsigned int totalDynamicMeshFaceSize_;

	int totalStaticMeshCount_;
	int totalSkeletalMeshCount_;
	int totalDynamicMeshCount_;

	GEuint staticVertexBufferId_;
	GEuint staticIndexBufferId_;

	GEuint skeletalVertexBufferId_;
	GEuint skeletalIndexBufferId_;

	GEuint dynamicVertexBufferId_;
	GEuint dynamicIndexBufferId_;

	RenderPassType mainRenderType_{ RenderPassType::Forward };

	unsigned char removeStaticDataFromMemoryAfterTransferingToGPU_ : 1;
};

#endif
