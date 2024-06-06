#include "pch.h"

#include "Renderer.h"

#include "Texture.h"
#include "Framebuffer.h"

#include "Goknar/Application.h"
#include "Goknar/Engine.h"
#include "Goknar/Scene.h"
#include "Goknar/Log.h"

#include "Goknar/Materials/Material.h"
#include "Goknar/Materials/MaterialBase.h"
#include "Goknar/Materials/MaterialInstance.h"

#include "Goknar/Delegates/Delegate.h"

#include "Goknar/Lights/DirectionalLight.h"
#include "Goknar/Lights/PointLight.h"
#include "Goknar/Lights/SpotLight.h"
#include "Goknar/Lights/ShadowManager/ShadowManager.h"

#include "Goknar/Model/DynamicMesh.h"
#include "Goknar/Model/StaticMesh.h"
#include "Goknar/Model/SkeletalMesh.h"
#include "Goknar/Model/DynamicMeshInstance.h"
#include "Goknar/Model/StaticMeshInstance.h"
#include "Goknar/Model/SkeletalMeshInstance.h"

#include "Goknar/Managers/CameraManager.h"
#include "Goknar/Managers/ResourceManager.h"
#include "Goknar/Managers/WindowManager.h"

#include "Goknar/IO/IOManager.h"

#include "Goknar/Renderer/Shader.h"

#define VERTEX_COLOR_LOCATION 0
#define VERTEX_POSITION_LOCATION 1
#define VERTEX_NORMAL_LOCATION 2
#define VERTEX_UV_LOCATION 3
#define BONE_ID_LOCATION 4
#define BONE_WEIGHT_LOCATION 5

Renderer::Renderer() :
	staticVertexBufferId_(0),
	staticIndexBufferId_(0),
	skeletalVertexBufferId_(0),
	skeletalIndexBufferId_(0),
	dynamicVertexBufferId_(0),
	dynamicIndexBufferId_(0),
	totalStaticMeshVertexSize_(0),
	totalStaticMeshFaceSize_(0),
	totalSkeletalMeshVertexSize_(0),
	totalSkeletalMeshFaceSize_(0),
	totalDynamicMeshVertexSize_(0),
	totalDynamicMeshFaceSize_(0),
	totalStaticMeshCount_(0),
	totalSkeletalMeshCount_(0),
	totalDynamicMeshCount_(0),
	shadowManager_(nullptr),
	removeStaticDataFromMemoryAfterTransferingToGPU_(false)
{
}

Renderer::~Renderer()
{
	delete shadowManager_;
	delete deferredRenderingData_;

	EXIT_ON_GL_ERROR("Renderer::~Renderer");

	glDeleteBuffers(1, &staticVertexBufferId_);
	glDeleteBuffers(1, &skeletalVertexBufferId_);
	glDeleteBuffers(1, &staticIndexBufferId_);

	for (StaticMeshInstance* opaqueStaticMeshInstance : opaqueStaticMeshInstances_)
	{
		delete opaqueStaticMeshInstance;
	}

	for (StaticMeshInstance* maskedStaticMeshInstance : maskedStaticMeshInstances_)
	{
		delete maskedStaticMeshInstance;
	}

	for (StaticMeshInstance* transparentStaticMeshInstance : transparentStaticMeshInstances_)
	{
		delete transparentStaticMeshInstance;
	}

	for (SkeletalMeshInstance* opaqueSkeletalMeshInstance : opaqueSkeletalMeshInstances_)
	{
		delete opaqueSkeletalMeshInstance;
	}

	for (SkeletalMeshInstance* maskedSkeletalMeshInstance : maskedSkeletalMeshInstances_)
	{
		delete maskedSkeletalMeshInstance;
	}

	for (SkeletalMeshInstance* transparentSkeletalMeshInstance : transparentSkeletalMeshInstances_)
	{
		delete transparentSkeletalMeshInstance;
	}

	for (DynamicMeshInstance* opaqueDynamicMeshInstance : opaqueDynamicMeshInstances_)
	{
		delete opaqueDynamicMeshInstance;
	}

	for (DynamicMeshInstance* maskedDynamicMeshInstance : maskedDynamicMeshInstances_)
	{
		delete maskedDynamicMeshInstance;
	}

	for (DynamicMeshInstance* transparentDynamicMeshInstance : transparentDynamicMeshInstances_)
	{
		delete transparentDynamicMeshInstance;
	}
}

void Renderer::PreInit()
{
	shadowManager_ = new ShadowManager();
	shadowManager_->PreInit();

	if (mainRenderType_ == RenderPassType::Deferred)
	{
		deferredRenderingData_ = new DeferredRenderingData();
		deferredRenderingData_->Init();
	}

	glFrontFace(GL_CCW);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	for (StaticMesh* staticMesh : staticMeshes_)
	{
		totalStaticMeshVertexSize_ += (unsigned int)staticMesh->GetVerticesPointer()->size();
		totalStaticMeshFaceSize_ += (unsigned int)staticMesh->GetFacesPointer()->size();
	}

	for (SkeletalMesh* skeletalMesh : skeletalMeshes_)
	{
		totalSkeletalMeshVertexSize_ += (unsigned int)skeletalMesh->GetVerticesPointer()->size();
		totalSkeletalMeshFaceSize_ += (unsigned int)skeletalMesh->GetFacesPointer()->size();
	}

	for (DynamicMesh* dynamicMesh : dynamicMeshes_)
	{
		totalDynamicMeshVertexSize_ += (unsigned int)dynamicMesh->GetVerticesPointer()->size();
		totalDynamicMeshFaceSize_ += (unsigned int)dynamicMesh->GetFacesPointer()->size();
	}

	SetBufferData();
}

void Renderer::Init()
{
	shadowManager_->Init();
}

void Renderer::PostInit()
{
	shadowManager_->PostInit();
}

void Renderer::SetStaticBufferData()
{
	/*
		Vertex buffer
	*/
	unsigned long long sizeOfVertexData = sizeof(VertexData);

	glGenBuffers(1, &staticVertexBufferId_);
	glBindBuffer(GL_ARRAY_BUFFER, staticVertexBufferId_);
	glBufferData(GL_ARRAY_BUFFER, totalStaticMeshVertexSize_ * sizeOfVertexData, nullptr, GL_STATIC_DRAW);

	/*
		Index buffer
	*/
	glGenBuffers(1, &staticIndexBufferId_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, staticIndexBufferId_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, totalStaticMeshFaceSize_ * sizeof(Face), nullptr, GL_STATIC_DRAW);

	/*
		Buffer Sub-Data
	*/
	unsigned int baseVertex = 0;
	unsigned int vertexStartingIndex = 0;

	int vertexOffset = 0;
	int faceOffset = 0;
	for (MeshUnit* staticMesh : staticMeshes_)
	{
		staticMesh->SetBaseVertex(baseVertex);
		staticMesh->SetVertexStartingIndex(vertexStartingIndex);

		const VertexArray* vertexArrayPtr = staticMesh->GetVerticesPointer();
		int vertexSizeInBytes = (int)vertexArrayPtr->size() * sizeof(vertexArrayPtr->at(0));
		glBufferSubData(GL_ARRAY_BUFFER, vertexOffset, vertexSizeInBytes, &vertexArrayPtr->at(0));

		const FaceArray* faceArrayPtr = staticMesh->GetFacesPointer();
		int faceSizeInBytes = (int)faceArrayPtr->size() * sizeof(faceArrayPtr->at(0));
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, faceOffset, faceSizeInBytes, &faceArrayPtr->at(0));

		vertexOffset += vertexSizeInBytes;
		faceOffset += faceSizeInBytes;

		baseVertex += staticMesh->GetVertexCount();
		vertexStartingIndex += staticMesh->GetFaceCount() * 3 * (int)sizeof(Face::vertexIndices[0]);

		if (removeStaticDataFromMemoryAfterTransferingToGPU_)
		{
			staticMesh->ClearDataFromMemory();
		}
	}
	SetAttribPointers();
}

void Renderer::SetSkeletalBufferData()
{
	/*
		Vertex buffer
	*/
	unsigned long long int sizeOfSkeletalMeshVertexData = sizeof(VertexData) + sizeof(VertexBoneData);

	glGenBuffers(1, &skeletalVertexBufferId_);
	glBindBuffer(GL_ARRAY_BUFFER, skeletalVertexBufferId_);
	glBufferData(GL_ARRAY_BUFFER, totalSkeletalMeshVertexSize_ * sizeOfSkeletalMeshVertexData, nullptr, GL_STATIC_DRAW);

	/*
		Index buffer
	*/
	glGenBuffers(1, &skeletalIndexBufferId_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skeletalIndexBufferId_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, totalSkeletalMeshFaceSize_ * sizeof(Face), nullptr, GL_STATIC_DRAW);

	/*
		Buffer Sub-Data
	*/
	unsigned int baseVertex = 0;
	unsigned int vertexStartingIndex = 0;

	int vertexOffset = 0;
	int faceOffset = 0;
	for (SkeletalMesh* skeletalMesh : skeletalMeshes_)
	{
		skeletalMesh->SetBaseVertex(baseVertex);
		skeletalMesh->SetVertexStartingIndex(vertexStartingIndex);

		const VertexArray* vertexArrayPtr = skeletalMesh->GetVerticesPointer();

		unsigned int vertexArrayPtrSize = vertexArrayPtr->size();
		if (vertexArrayPtrSize == 0)
		{
			continue;
		}

		GLintptr vertexSizeInBytes = sizeof(vertexArrayPtr->at(0));

		const VertexBoneDataArray* vertexBoneDataArray = skeletalMesh->GetVertexBoneDataArray();
		int vertexBoneDataArraySizeInBytes = sizeof(vertexBoneDataArray->at(0));
		for (unsigned int i = 0; i < vertexArrayPtrSize; ++i)
		{
			glBufferSubData(GL_ARRAY_BUFFER, vertexOffset, vertexSizeInBytes, &vertexArrayPtr->at(i));
			vertexOffset += vertexSizeInBytes;

			glBufferSubData(GL_ARRAY_BUFFER, vertexOffset, vertexBoneDataArraySizeInBytes, &vertexBoneDataArray->at(i));
			vertexOffset += vertexBoneDataArraySizeInBytes;
		}

		const FaceArray* faceArrayPtr = skeletalMesh->GetFacesPointer();
		int faceSizeInBytes = (int)faceArrayPtr->size() * sizeof(faceArrayPtr->at(0));
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, faceOffset, faceSizeInBytes, &faceArrayPtr->at(0));
		faceOffset += faceSizeInBytes;

		baseVertex += skeletalMesh->GetVertexCount();
		vertexStartingIndex += skeletalMesh->GetFaceCount() * 3 * (int)sizeof(Face::vertexIndices[0]);

		if (removeStaticDataFromMemoryAfterTransferingToGPU_)
		{
			skeletalMesh->ClearDataFromMemory();
		}
	}
	SetAttribPointersForSkeletalMesh();
}

void Renderer::SetDynamicBufferData()
{
	/*
		Vertex buffer
	*/
	unsigned long long sizeOfVertexData = sizeof(VertexData);

	glGenBuffers(1, &dynamicVertexBufferId_);
	glBindBuffer(GL_ARRAY_BUFFER, dynamicVertexBufferId_);
	glBufferData(GL_ARRAY_BUFFER, totalDynamicMeshVertexSize_ * sizeOfVertexData, nullptr, GL_DYNAMIC_DRAW);

	/*
		Index buffer
	*/
	glGenBuffers(1, &dynamicIndexBufferId_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dynamicIndexBufferId_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, totalDynamicMeshFaceSize_ * sizeof(Face), nullptr, GL_DYNAMIC_DRAW);

	/*
		Buffer Sub-Data
	*/
	unsigned int baseVertex = 0;
	unsigned int vertexStartingIndex = 0;

	int vertexOffset = 0;
	int faceOffset = 0;
	for (DynamicMesh* dynamicMesh : dynamicMeshes_)
	{
		dynamicMesh->SetBaseVertex(baseVertex);
		dynamicMesh->SetVertexStartingIndex(vertexStartingIndex);
		dynamicMesh->SetRendererVertexOffset(vertexOffset);

		const VertexArray* vertexArrayPtr = dynamicMesh->GetVerticesPointer();
		int vertexSizeInBytes = (int)vertexArrayPtr->size() * sizeof(vertexArrayPtr->at(0));
		glBufferSubData(GL_ARRAY_BUFFER, vertexOffset, vertexSizeInBytes, &vertexArrayPtr->at(0));

		const FaceArray* faceArrayPtr = dynamicMesh->GetFacesPointer();
		int faceSizeInBytes = (int)faceArrayPtr->size() * sizeof(faceArrayPtr->at(0));
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, faceOffset, faceSizeInBytes, &faceArrayPtr->at(0));

		vertexOffset += vertexSizeInBytes;
		faceOffset += faceSizeInBytes;

		baseVertex += dynamicMesh->GetVertexCount();
		vertexStartingIndex += dynamicMesh->GetFaceCount() * 3 * (int)sizeof(Face::vertexIndices[0]);
	}

	SetAttribPointers();
}

void Renderer::SetBufferData()
{
	if (0 < totalStaticMeshCount_) SetStaticBufferData();
	if (0 < totalSkeletalMeshCount_) SetSkeletalBufferData();
	if (0 < totalDynamicMeshCount_) SetDynamicBufferData();
}

void Renderer::Render(RenderPassType renderPassType)
{
	switch (renderPassType)
	{
		case RenderPassType::Forward:
		{
			glDepthMask(GL_TRUE);
			const Colorf& sceneBackgroundColor = engine->GetApplication()->GetMainScene()->GetBackgroundColor();
			glClearColor(sceneBackgroundColor.r, sceneBackgroundColor.g, sceneBackgroundColor.b, 1.f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			break;
		}
		case RenderPassType::GeometryBuffer:
		{
			GOKNAR_CORE_CHECK(deferredRenderingData_ != nullptr, "Main rendering is not set to deferred rendering but deferred rendering is called.");

			deferredRenderingData_->BindGeometryBuffer();
			const Colorf& sceneBackgroundColor = engine->GetApplication()->GetMainScene()->GetBackgroundColor();
			glClearColor(sceneBackgroundColor.r, sceneBackgroundColor.g, sceneBackgroundColor.b, 1.f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glDepthMask(GL_TRUE);
			break;
		}
		case RenderPassType::Deferred:
		{
			GOKNAR_CORE_CHECK(deferredRenderingData_ != nullptr, "Main rendering is not set to deferred rendering but deferred rendering is called.");
			glClear(GL_DEPTH_BUFFER_BIT);
			deferredRenderingData_->Render();
			break;
		}
		case RenderPassType::Shadow:
		case RenderPassType::PointLightShadow:
		{
			glClear(GL_DEPTH_BUFFER_BIT);
			break;
		}
		case RenderPassType::None:
		default:
		{
			GOKNAR_CORE_ASSERT(false, "Render function called without a correct pass type!");
			return;
		}
	}

	const Camera* activeCamera = engine->GetCameraManager()->GetActiveCamera();
	
	if (activeCamera)
	{
		bool isShadowRender = 
			renderPassType == RenderPassType::Shadow || 
			renderPassType == RenderPassType::PointLightShadow;

		if(renderPassType != RenderPassType::Deferred)
		{
			// Static MeshUnit Instances
			{
				if (0 < totalStaticMeshCount_)
				{
					BindStaticVBO();

					for (StaticMeshInstance* opaqueStaticMeshInstance : opaqueStaticMeshInstances_)
					{
						if (!opaqueStaticMeshInstance->GetIsRendered()) continue;
						if (isShadowRender && !opaqueStaticMeshInstance->GetIsCastingShadow()) continue;

						const MeshUnit* mesh = opaqueStaticMeshInstance->GetMesh();
						opaqueStaticMeshInstance->Render(renderPassType);

						int facePointCount = mesh->GetFaceCount() * 3;
						glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)mesh->GetVertexStartingIndex(), mesh->GetBaseVertex());
					}

					for (StaticMeshInstance* maskedStaticMeshInstance : maskedStaticMeshInstances_)
					{
						if (!maskedStaticMeshInstance->GetIsRendered()) continue;
						if (isShadowRender && !maskedStaticMeshInstance->GetIsCastingShadow()) continue;

						const MeshUnit* mesh = maskedStaticMeshInstance->GetMesh();
						maskedStaticMeshInstance->Render(renderPassType);

						int facePointCount = mesh->GetFaceCount() * 3;
						glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)mesh->GetVertexStartingIndex(), mesh->GetBaseVertex());
					}
				}
			}

			// Skeletal MeshUnit Instances
			{
				if (0 < totalSkeletalMeshCount_)
				{
					BindSkeletalVBO();

					for (SkeletalMeshInstance* opaqueSkeletalMeshInstance : opaqueSkeletalMeshInstances_)
					{
						if (!opaqueSkeletalMeshInstance->GetIsRendered()) continue;
						if (isShadowRender && !opaqueSkeletalMeshInstance->GetIsCastingShadow()) continue;

						const SkeletalMesh* skeletalMesh = opaqueSkeletalMeshInstance->GetMesh();
						opaqueSkeletalMeshInstance->Render(renderPassType);

						int facePointCount = skeletalMesh->GetFaceCount() * 3;
						glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)skeletalMesh->GetVertexStartingIndex(), skeletalMesh->GetBaseVertex());
					}

					for (SkeletalMeshInstance* maskedSkeletalMeshInstance : maskedSkeletalMeshInstances_)
					{
						if (!maskedSkeletalMeshInstance->GetIsRendered()) continue;
						if (isShadowRender && !maskedSkeletalMeshInstance->GetIsCastingShadow()) continue;

			
						const SkeletalMesh* skeletalMesh = maskedSkeletalMeshInstance->GetMesh();
						maskedSkeletalMeshInstance->Render(renderPassType);

						int facePointCount = skeletalMesh->GetFaceCount() * 3;
						glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)skeletalMesh->GetVertexStartingIndex(), skeletalMesh->GetBaseVertex());
					}
				}
			}

			// Dynamic MeshUnit Instances
			{
				if (0 < totalDynamicMeshCount_)
				{
					BindDynamicVBO();

					for (DynamicMeshInstance* opaqueDynamicMeshInstance : opaqueDynamicMeshInstances_)
					{
						if (!opaqueDynamicMeshInstance->GetIsRendered()) continue;
						if (isShadowRender && !opaqueDynamicMeshInstance->GetIsCastingShadow()) continue;

						const MeshUnit* mesh = opaqueDynamicMeshInstance->GetMesh();
						opaqueDynamicMeshInstance->Render(renderPassType);

						int facePointCount = mesh->GetFaceCount() * 3;
						glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)mesh->GetVertexStartingIndex(), mesh->GetBaseVertex());
					}

					for (DynamicMeshInstance* maskedDynamicMeshInstance : maskedDynamicMeshInstances_)
					{
						if (!maskedDynamicMeshInstance->GetIsRendered()) continue;
						if (isShadowRender && !maskedDynamicMeshInstance->GetIsCastingShadow()) continue;

						const MeshUnit* mesh = maskedDynamicMeshInstance->GetMesh();
						maskedDynamicMeshInstance->Render(renderPassType);

						int facePointCount = mesh->GetFaceCount() * 3;
						glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)mesh->GetVertexStartingIndex(), mesh->GetBaseVertex());
					}
				}
			}
		}
		else
		{
			deferredRenderingData_->BindGBufferDepth();
		}
			
		if (renderPassType == RenderPassType::Forward ||
			renderPassType == RenderPassType::Deferred)
		{
			SortTransparentInstances();

			glEnable(GL_BLEND);
			glDepthMask(GL_FALSE);

			BindStaticVBO();

			for (StaticMeshInstance* transparentStaticMeshInstance : transparentStaticMeshInstances_)
			{
				if (!transparentStaticMeshInstance->GetIsRendered()) continue;
				const MeshUnit* mesh = transparentStaticMeshInstance->GetMesh();
				
				transparentStaticMeshInstance->Render(RenderPassType::Forward);

				int facePointCount = mesh->GetFaceCount() * 3;
				glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)mesh->GetVertexStartingIndex(), mesh->GetBaseVertex());
			}

			BindSkeletalVBO();
			for (SkeletalMeshInstance* transparentSkeletalMeshInstance : transparentSkeletalMeshInstances_)
			{
				if (!transparentSkeletalMeshInstance->GetIsRendered()) continue;

				const SkeletalMesh* skeletalMesh = transparentSkeletalMeshInstance->GetMesh();
				transparentSkeletalMeshInstance->Render(RenderPassType::Forward);

				int facePointCount = skeletalMesh->GetFaceCount() * 3;
				glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)skeletalMesh->GetVertexStartingIndex(), skeletalMesh->GetBaseVertex());
			}

			BindDynamicVBO();
			for (DynamicMeshInstance* transparentDynamicMeshInstance : transparentDynamicMeshInstances_)
			{
				if (!transparentDynamicMeshInstance->GetIsRendered()) continue;
				const MeshUnit* mesh = transparentDynamicMeshInstance->GetMesh();

				transparentDynamicMeshInstance->Render(RenderPassType::Forward);

				int facePointCount = mesh->GetFaceCount() * 3;
				glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)mesh->GetVertexStartingIndex(), mesh->GetBaseVertex());
			}
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);

			// After finishing transparent object rendering
			// Use deferred rendering shader again if doing deferred rendering
			// Otherwise it causes crash on setting g-buffer debug on
			// And wishing to visualize g-buffers on deferred rendering mesh
			if(renderPassType == RenderPassType::Deferred)
			{
				deferredRenderingData_->deferredRenderingMeshShader->Use();
			}
		}
	}

	if (renderPassType == RenderPassType::GeometryBuffer)
	{
		deferredRenderingData_->UnbindGeometryBuffer();
		return;
	}
}

void Renderer::AddStaticMeshToRenderer(StaticMesh* staticMesh)
{
	staticMeshes_.push_back(staticMesh);
	totalStaticMeshCount_++;
}

void Renderer::AddStaticMeshInstance(StaticMeshInstance* meshInstance)
{
	MaterialBlendModel materialShadingModel = meshInstance->GetMaterial()->GetBlendModel();
	switch (materialShadingModel)
	{
	case MaterialBlendModel::Opaque:
		opaqueStaticMeshInstances_.push_back(meshInstance);
		break;
	case MaterialBlendModel::Masked:
		maskedStaticMeshInstances_.push_back(meshInstance);
		break;
	case MaterialBlendModel::Transparent:
		transparentStaticMeshInstances_.push_back(meshInstance);
		break;
	default:
		break;
	}
}

void Renderer::RemoveStaticMeshInstance(StaticMeshInstance* staticMeshInstance)
{
	MaterialBlendModel blendModel = staticMeshInstance->GetMaterial()->GetBlendModel();

	switch (blendModel)
	{
	case MaterialBlendModel::Opaque:
	{
		size_t meshInstanceCount = opaqueStaticMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (opaqueStaticMeshInstances_[meshInstanceIndex] == staticMeshInstance)
			{
				opaqueStaticMeshInstances_.erase(opaqueStaticMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	case MaterialBlendModel::Masked:
	{
		size_t meshInstanceCount = maskedStaticMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (maskedStaticMeshInstances_[meshInstanceIndex] == staticMeshInstance)
			{
				maskedStaticMeshInstances_.erase(maskedStaticMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	case MaterialBlendModel::Transparent:
	{
		size_t meshInstanceCount = transparentStaticMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (transparentStaticMeshInstances_[meshInstanceIndex] == staticMeshInstance)
			{
				transparentStaticMeshInstances_.erase(transparentStaticMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	default:
		break;
	}
}

void Renderer::AddSkeletalMeshToRenderer(SkeletalMesh* skeletalMesh)
{
	skeletalMeshes_.push_back(skeletalMesh);
	totalSkeletalMeshCount_++;
}

void Renderer::AddSkeletalMeshInstance(SkeletalMeshInstance* skeletalMeshInstance)
{
	MaterialBlendModel materialShadingModel = skeletalMeshInstance->GetMaterial()->GetBlendModel();
	switch (materialShadingModel)
	{
	case MaterialBlendModel::Opaque:
		opaqueSkeletalMeshInstances_.push_back(skeletalMeshInstance);
		break;
	case MaterialBlendModel::Masked:
		maskedSkeletalMeshInstances_.push_back(skeletalMeshInstance);
		break;
	case MaterialBlendModel::Transparent:
		transparentSkeletalMeshInstances_.push_back(skeletalMeshInstance);
		break;
	default:
		break;
	}
}

void Renderer::RemoveSkeletalMeshInstance(SkeletalMeshInstance* skeletalMeshInstance)
{
	MaterialBlendModel blendModel = skeletalMeshInstance->GetMaterial()->GetBlendModel();

	switch (blendModel)
	{
	case MaterialBlendModel::Opaque:
	{
		size_t meshInstanceCount = opaqueSkeletalMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (opaqueSkeletalMeshInstances_[meshInstanceIndex] == skeletalMeshInstance)
			{
				opaqueSkeletalMeshInstances_.erase(opaqueSkeletalMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	case MaterialBlendModel::Masked:
	{
		size_t meshInstanceCount = maskedSkeletalMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (maskedSkeletalMeshInstances_[meshInstanceIndex] == skeletalMeshInstance)
			{
				maskedSkeletalMeshInstances_.erase(maskedSkeletalMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	case MaterialBlendModel::Transparent:
	{
		size_t meshInstanceCount = transparentSkeletalMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (transparentSkeletalMeshInstances_[meshInstanceIndex] == skeletalMeshInstance)
			{
				transparentSkeletalMeshInstances_.erase(transparentSkeletalMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	default:
		break;
	}
}

void Renderer::AddDynamicMeshToRenderer(DynamicMesh* dynamicMesh)
{
	dynamicMeshes_.push_back(dynamicMesh);
	totalDynamicMeshCount_++;
}

void Renderer::AddDynamicMeshInstance(DynamicMeshInstance* dynamicMeshInstance)
{
	MaterialBlendModel materialShadingModel = dynamicMeshInstance->GetMaterial()->GetBlendModel();
	switch (materialShadingModel)
	{
	case MaterialBlendModel::Opaque:
		opaqueDynamicMeshInstances_.push_back(dynamicMeshInstance);
		break;
	case MaterialBlendModel::Masked:
		maskedDynamicMeshInstances_.push_back(dynamicMeshInstance);
		break;
	case MaterialBlendModel::Transparent:
		transparentDynamicMeshInstances_.push_back(dynamicMeshInstance);
		break;
	default:
		break;
	}
}

void Renderer::RemoveDynamicMeshInstance(DynamicMeshInstance* dynamicMeshInstance)
{
	MaterialBlendModel blendModel = dynamicMeshInstance->GetMaterial()->GetBlendModel();

	switch (blendModel)
	{
	case MaterialBlendModel::Opaque:
	{
		size_t meshInstanceCount = opaqueDynamicMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (opaqueDynamicMeshInstances_[meshInstanceIndex] == dynamicMeshInstance)
			{
				opaqueDynamicMeshInstances_.erase(opaqueDynamicMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	case MaterialBlendModel::Masked:
	{
		size_t meshInstanceCount = maskedDynamicMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (maskedDynamicMeshInstances_[meshInstanceIndex] == dynamicMeshInstance)
			{
				maskedDynamicMeshInstances_.erase(maskedDynamicMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	case MaterialBlendModel::Transparent:
	{
		size_t meshInstanceCount = transparentDynamicMeshInstances_.size();
		for (size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			if (transparentDynamicMeshInstances_[meshInstanceIndex] == dynamicMeshInstance)
			{
				transparentDynamicMeshInstances_.erase(transparentDynamicMeshInstances_.begin() + meshInstanceIndex);
				return;
			}
		}
		break;
	}
	default:
		break;
	}
}

void Renderer::UpdateDynamicMeshVertex(const DynamicMesh* object, int vertexIndex, const VertexData& newVertexData)
{
	//BindDynamicVBO();
	int sizeOfVertexData = sizeof(VertexData);
	glNamedBufferSubData(dynamicVertexBufferId_, object->GetRendererVertexOffset() + vertexIndex * sizeOfVertexData, sizeOfVertexData, &newVertexData);
}

void Renderer::PrepareSkeletalMeshInstancesForTheCurrentFrame()
{
		size_t meshInstanceCount = opaqueSkeletalMeshInstances_.size();
		size_t meshInstanceIndex = 0;

		for (; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			opaqueSkeletalMeshInstances_[meshInstanceIndex]->PrepareForTheCurrentFrame();
		}

		meshInstanceCount = maskedSkeletalMeshInstances_.size();
		for (meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			maskedSkeletalMeshInstances_[meshInstanceIndex]->PrepareForTheCurrentFrame();
		}

		meshInstanceCount = transparentSkeletalMeshInstances_.size();
		for (meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			transparentSkeletalMeshInstances_[meshInstanceIndex]->PrepareForTheCurrentFrame();
		}
}


void Renderer::PrepareSkeletalMeshInstancesForTheNextFrame()
{
		size_t meshInstanceCount = opaqueSkeletalMeshInstances_.size();
		size_t meshInstanceIndex = 0;

		for (; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			opaqueSkeletalMeshInstances_[meshInstanceIndex]->PrepareForTheNextFrame();
		}

		meshInstanceCount = maskedSkeletalMeshInstances_.size();
		for (meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			maskedSkeletalMeshInstances_[meshInstanceIndex]->PrepareForTheNextFrame();
		}

		meshInstanceCount = transparentSkeletalMeshInstances_.size();
		for (meshInstanceIndex = 0; meshInstanceIndex < meshInstanceCount; meshInstanceIndex++)
		{
			transparentSkeletalMeshInstances_[meshInstanceIndex]->PrepareForTheNextFrame();
		}
}

void Renderer::BindShadowTextures(Shader* shader)
{
	shader->Use();

	std::vector<int> directionalLightTextureIndices;
	Scene* scene = engine->GetApplication()->GetMainScene();
	const std::vector<DirectionalLight*>& directionalLights = scene->GetDirectionalLights();
	size_t directionalLightCount = directionalLights.size();
	for (size_t i = 0; i < directionalLightCount; i++)
	{
		DirectionalLight* directionalLight = directionalLights[i];
		if (directionalLight->GetIsShadowEnabled())
		{
			Texture* shadowTexture = directionalLight->GetShadowMapTexture();
			shadowTexture->Bind(shader);
			directionalLightTextureIndices.push_back(shadowTexture->GetRendererTextureId());
		}
	}
	shader->SetIntVector(SHADER_VARIABLE_NAMES::LIGHT::DIRECTIONAL_LIGHT_SHADOW_MAP_ARRAY_NAME, directionalLightTextureIndices);

	std::vector<int> pointLightTextureIndices;
	const std::vector<PointLight*>& pointLights = scene->GetPointLights();
	size_t pointLightCount = pointLights.size();
	for (size_t i = 0; i < pointLightCount; i++)
	{
		PointLight* pointLight = pointLights[i];
		if (pointLight->GetIsShadowEnabled())
		{
			Texture* shadowTexture = pointLight->GetShadowMapTexture();
			shadowTexture->Bind(shader);
			pointLightTextureIndices.push_back(shadowTexture->GetRendererTextureId());
		}
	}
	shader->SetIntVector(SHADER_VARIABLE_NAMES::LIGHT::POINT_LIGHT_SHADOW_MAP_ARRAY_NAME, pointLightTextureIndices);

	std::vector<int> spotLightTextureIndices;
	const std::vector<SpotLight*>& spotLights = scene->GetSpotLights();
	size_t spotLightCount = spotLights.size();
	for (size_t i = 0; i < spotLightCount; i++)
	{
		SpotLight* spotLight = spotLights[i];
		if (spotLight->GetIsShadowEnabled())
		{
			Texture* shadowTexture = spotLight->GetShadowMapTexture();
			shadowTexture->Bind(shader);
			spotLightTextureIndices.push_back(shadowTexture->GetRendererTextureId());
		}
	}
	shader->SetIntVector(SHADER_VARIABLE_NAMES::LIGHT::SPOT_LIGHT_SHADOW_MAP_ARRAY_NAME, spotLightTextureIndices);
}

void Renderer::BindGeometryBufferTextures(Shader* shader)
{
	if (deferredRenderingData_)
	{
		shader->Use();
		deferredRenderingData_->BindGeometryBufferTextures(shader);
	}
}

void Renderer::SetLightUniforms(Shader* shader)
{
	Scene* scene = engine->GetApplication()->GetMainScene();
	const std::vector<DirectionalLight*>& staticDirectionalLights = scene->GetDirectionalLights();
	size_t staticDirectionalLightCount = staticDirectionalLights.size();
	for (size_t i = 0; i < staticDirectionalLightCount; i++)
	{
		staticDirectionalLights[i]->SetShaderUniforms(shader);
	}

	const std::vector<DirectionalLight*>& dynamicDirectionalLights = scene->GetDirectionalLights();
	size_t dynamicDirectionalLightCount = dynamicDirectionalLights.size();
	for (size_t i = 0; i < dynamicDirectionalLightCount; i++)
	{
		dynamicDirectionalLights[i]->SetShaderUniforms(shader);
	}

	const std::vector<PointLight*>& staticPointLights = scene->GetPointLights();
	size_t staticPointLightCount = staticPointLights.size();
	for (size_t i = 0; i < staticPointLightCount; i++)
	{
		staticPointLights[i]->SetShaderUniforms(shader);
	}

	const std::vector<PointLight*>& dynamicPointLights = scene->GetPointLights();
	size_t dynamicPointLightCount = dynamicPointLights.size();
	for (size_t i = 0; i < dynamicPointLightCount; i++)
	{
		dynamicPointLights[i]->SetShaderUniforms(shader);
	}

	const std::vector<SpotLight*>& staticSpotLights = scene->GetSpotLights();
	size_t staticSpotLightCount = staticSpotLights.size();
	for (size_t i = 0; i < staticSpotLightCount; i++)
	{
		staticSpotLights[i]->SetShaderUniforms(shader);
	}

	const std::vector<SpotLight*>& dynamicSpotLights = scene->GetSpotLights();
	size_t dynamicSpotLightCount = dynamicSpotLights.size();
	for (size_t i = 0; i < dynamicSpotLightCount; i++)
	{
		dynamicSpotLights[i]->SetShaderUniforms(shader);
	}
}

void Renderer::BindStaticVBO()
{
	glBindBuffer(GL_ARRAY_BUFFER, staticVertexBufferId_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, staticIndexBufferId_);
	SetAttribPointers();
}

void Renderer::BindSkeletalVBO()
{
	glBindBuffer(GL_ARRAY_BUFFER, skeletalVertexBufferId_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skeletalIndexBufferId_);
	SetAttribPointersForSkeletalMesh();
}

void Renderer::BindDynamicVBO()
{
	glBindBuffer(GL_ARRAY_BUFFER, dynamicVertexBufferId_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dynamicIndexBufferId_);
	SetAttribPointers();
}

void Renderer::SetAttribPointers()
{
	GEsizei sizeOfVertexData = (GEsizei)sizeof(VertexData);
	// Vertex color
	long long offset = 0;
	glEnableVertexAttribArray(VERTEX_COLOR_LOCATION);
	glVertexAttribPointer(VERTEX_COLOR_LOCATION, 4, GL_FLOAT, GL_FALSE, sizeOfVertexData, (void*)offset);

	// Vertex position
	offset += sizeof(VertexData::color);
	glEnableVertexAttribArray(VERTEX_POSITION_LOCATION);
	glVertexAttribPointer(VERTEX_POSITION_LOCATION, 3, GL_FLOAT, GL_FALSE, sizeOfVertexData, (void*)offset);

	// Vertex normal
	offset += sizeof(VertexData::position);
	glEnableVertexAttribArray(VERTEX_NORMAL_LOCATION);
	glVertexAttribPointer(VERTEX_NORMAL_LOCATION, 3, GL_FLOAT, GL_FALSE, sizeOfVertexData, (void*)offset);

	// Vertex UV
	offset += sizeof(VertexData::normal);
	glEnableVertexAttribArray(VERTEX_UV_LOCATION);
	glVertexAttribPointer(VERTEX_UV_LOCATION, 2, GL_FLOAT, GL_FALSE, sizeOfVertexData, (void*)offset);
}

void Renderer::SetAttribPointersForSkeletalMesh()
{
	GEsizei sizeOfSkeletalMeshVertexData = (GEsizei)(sizeof(VertexData) + sizeof(VertexBoneData));

	long long offset = 0;
	// Vertex color
	glEnableVertexAttribArray(VERTEX_COLOR_LOCATION);
	glVertexAttribPointer(VERTEX_COLOR_LOCATION, 4, GL_FLOAT, GL_FALSE, sizeOfSkeletalMeshVertexData, (void*)offset);
	offset += sizeof(VertexData::color);

	// Vertex position
	glEnableVertexAttribArray(VERTEX_POSITION_LOCATION);
	glVertexAttribPointer(VERTEX_POSITION_LOCATION, 3, GL_FLOAT, GL_FALSE, sizeOfSkeletalMeshVertexData, (void*)offset);
	offset += sizeof(VertexData::position);

	// Vertex normal
	glEnableVertexAttribArray(VERTEX_NORMAL_LOCATION);
	glVertexAttribPointer(VERTEX_NORMAL_LOCATION, 3, GL_FLOAT, GL_FALSE, sizeOfSkeletalMeshVertexData, (void*)offset);
	offset += sizeof(VertexData::normal);

	// Vertex UV
	glEnableVertexAttribArray(VERTEX_UV_LOCATION);
	glVertexAttribPointer(VERTEX_UV_LOCATION, 2, GL_FLOAT, GL_FALSE, sizeOfSkeletalMeshVertexData, (void*)offset);
	offset += sizeof(VertexData::uv);

	// Bone ID
	glEnableVertexAttribArray(BONE_ID_LOCATION);
	glVertexAttribIPointer(BONE_ID_LOCATION, MAX_BONE_SIZE_PER_VERTEX, GL_UNSIGNED_INT, sizeOfSkeletalMeshVertexData, (void*)offset);
	offset += sizeof(VertexBoneData::boneIDs);

	// Bone Weight
	glEnableVertexAttribArray(BONE_WEIGHT_LOCATION);
	glVertexAttribPointer(BONE_WEIGHT_LOCATION, MAX_BONE_SIZE_PER_VERTEX, GL_FLOAT, GL_FALSE, sizeOfSkeletalMeshVertexData, (void*)offset);
}

void Renderer::SortTransparentInstances()
{
	struct
	{
		Vector3 cameraPosition = engine->GetCameraManager()->GetActiveCamera()->GetPosition();
		bool operator()(const StaticMeshInstance* a, const StaticMeshInstance* b) const
		{
			return  (cameraPosition - a->GetParentComponent()->GetWorldPosition()).SquareLength() > 
					(cameraPosition - b->GetParentComponent()->GetWorldPosition()).SquareLength();
		}
		bool operator()(const SkeletalMeshInstance* a, const SkeletalMeshInstance* b) const
		{
			return  (cameraPosition - a->GetParentComponent()->GetWorldPosition()).SquareLength() > 
					(cameraPosition - b->GetParentComponent()->GetWorldPosition()).SquareLength();
		}
		bool operator()(const DynamicMeshInstance* a, const DynamicMeshInstance* b) const
		{
			return  (cameraPosition - a->GetParentComponent()->GetWorldPosition()).SquareLength() > 
					(cameraPosition - b->GetParentComponent()->GetWorldPosition()).SquareLength();
		}
	} cameraDistanceSorter;

	std::sort(
		transparentStaticMeshInstances_.begin(),
		transparentStaticMeshInstances_.end(),
		cameraDistanceSorter);

	std::sort(
		transparentSkeletalMeshInstances_.begin(),
		transparentSkeletalMeshInstances_.end(),
		cameraDistanceSorter);

	std::sort(
		transparentDynamicMeshInstances_.begin(),
		transparentDynamicMeshInstances_.end(),
		cameraDistanceSorter);
}

GeometryBufferData::GeometryBufferData()
{
}

GeometryBufferData::~GeometryBufferData()
{
	delete worldPositionTexture;
	delete worldNormalTexture;
	delete diffuseTexture;
	delete specularTexture;

	delete geometryFrameBuffer;
}

void GeometryBufferData::Init()
{
	Vector2i windowSize = engine->GetWindowManager()->GetWindowSize();

	bufferWidth = windowSize.x;
	bufferHeight = windowSize.y;

	GenerateBuffers();
	
	engine->GetWindowManager()->AddWindowSizeCallback(Delegate<void(int, int)>::create<GeometryBufferData, &GeometryBufferData::OnWindowSizeChange>(this));
}

void GeometryBufferData::Bind()
{
	geometryFrameBuffer->Bind();
}

void GeometryBufferData::Unbind()
{
	geometryFrameBuffer->Unbind();
}

void GeometryBufferData::GenerateBuffers()
{
	geometryFrameBuffer = new Framebuffer();

	worldPositionTexture = new Texture();
	worldPositionTexture->SetName(SHADER_VARIABLE_NAMES::GBUFFER::OUT_POSITION);
	worldPositionTexture->SetTextureDataType(TextureDataType::DYNAMIC);
	worldPositionTexture->SetTextureFormat(TextureFormat::RGB);
	worldPositionTexture->SetTextureInternalFormat(TextureInternalFormat::RGB32F);
	worldPositionTexture->SetTextureMinFilter(TextureMinFilter::NEAREST);
	worldPositionTexture->SetTextureMagFilter(TextureMagFilter::NEAREST);
	worldPositionTexture->SetWidth(bufferWidth);
	worldPositionTexture->SetHeight(bufferHeight);
	worldPositionTexture->SetGenerateMipmap(false);
	worldPositionTexture->PreInit();
	worldPositionTexture->Init();
	worldPositionTexture->PostInit();
	geometryFrameBuffer->AddAttachment(FramebufferAttachment::COLOR_ATTACHMENT0, worldPositionTexture);

	worldNormalTexture = new Texture();
	worldNormalTexture->SetName(SHADER_VARIABLE_NAMES::GBUFFER::OUT_NORMAL);
	worldNormalTexture->SetTextureDataType(TextureDataType::DYNAMIC);
	worldNormalTexture->SetTextureFormat(TextureFormat::RGBA);
	worldNormalTexture->SetTextureInternalFormat(TextureInternalFormat::RGBA16F);
	worldNormalTexture->SetTextureMinFilter(TextureMinFilter::NEAREST);
	worldNormalTexture->SetTextureMagFilter(TextureMagFilter::NEAREST);
	worldNormalTexture->SetWidth(bufferWidth);
	worldNormalTexture->SetHeight(bufferHeight);
	worldNormalTexture->SetGenerateMipmap(false);
	worldNormalTexture->PreInit();
	worldNormalTexture->Init();
	worldNormalTexture->PostInit();
	geometryFrameBuffer->AddAttachment(FramebufferAttachment::COLOR_ATTACHMENT1, worldNormalTexture);

	diffuseTexture = new Texture();
	diffuseTexture->SetName(SHADER_VARIABLE_NAMES::GBUFFER::OUT_DIFFUSE);
	diffuseTexture->SetTextureDataType(TextureDataType::DYNAMIC);
	diffuseTexture->SetTextureFormat(TextureFormat::RGB);
	diffuseTexture->SetTextureInternalFormat(TextureInternalFormat::RGB);
	diffuseTexture->SetTextureMinFilter(TextureMinFilter::NEAREST);
	diffuseTexture->SetTextureMagFilter(TextureMagFilter::NEAREST);
	diffuseTexture->SetWidth(bufferWidth);
	diffuseTexture->SetHeight(bufferHeight);
	diffuseTexture->SetGenerateMipmap(false);
	diffuseTexture->SetTextureType(TextureType::UNSIGNED_BYTE);
	diffuseTexture->PreInit();
	diffuseTexture->Init();
	diffuseTexture->PostInit();
	geometryFrameBuffer->AddAttachment(FramebufferAttachment::COLOR_ATTACHMENT2, diffuseTexture);

	specularTexture = new Texture();
	specularTexture->SetName(SHADER_VARIABLE_NAMES::GBUFFER::OUT_SPECULAR_PHONG);
	specularTexture->SetTextureDataType(TextureDataType::DYNAMIC);
	specularTexture->SetTextureFormat(TextureFormat::RGBA);
	specularTexture->SetTextureInternalFormat(TextureInternalFormat::RGBA);
	specularTexture->SetTextureMinFilter(TextureMinFilter::NEAREST);
	specularTexture->SetTextureMagFilter(TextureMagFilter::NEAREST);
	specularTexture->SetWidth(bufferWidth);
	specularTexture->SetHeight(bufferHeight);
	specularTexture->SetGenerateMipmap(false);
	specularTexture->SetTextureType(TextureType::UNSIGNED_BYTE);
	specularTexture->PreInit();
	specularTexture->Init();
	specularTexture->PostInit();
	geometryFrameBuffer->AddAttachment(FramebufferAttachment::COLOR_ATTACHMENT3, specularTexture);

	geometryFrameBuffer->PreInit();
	geometryFrameBuffer->Init();
	geometryFrameBuffer->PostInit();
	geometryFrameBuffer->Bind();
	geometryFrameBuffer->Attach();

	geometryFrameBuffer->DrawBuffers();

	glGenRenderbuffers(1, &depthRenderbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, bufferWidth, bufferHeight);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer);

	geometryFrameBuffer->Unbind();

	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	EXIT_ON_GL_ERROR("GeometryBufferData::GenerateBuffers");
}

void GeometryBufferData::OnWindowSizeChange(int width, int height)
{
	delete worldPositionTexture;
	delete worldNormalTexture;
	delete diffuseTexture;
	delete specularTexture;

	delete geometryFrameBuffer;
	
	glDeleteRenderbuffers(1, &depthRenderbuffer);

	bufferWidth = width;
	bufferHeight = height;
	GenerateBuffers();
}

void GeometryBufferData::BindGBufferDepth()
{
	geometryFrameBuffer->Bind(FramebufferBindTarget::READ_FRAMEBUFFER);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(0, 0, bufferWidth, bufferHeight, 
						0, 0, bufferWidth, bufferHeight,
						GL_DEPTH_BUFFER_BIT, GL_NEAREST);
}

DeferredRenderingData::DeferredRenderingData()
{
}

DeferredRenderingData::~DeferredRenderingData()
{
	delete geometryBufferData;
	delete deferredRenderingMesh;
	delete deferredRenderingMeshShader;
}

void DeferredRenderingData::Init()
{
	deferredRenderingMesh = new StaticMesh();
	deferredRenderingMesh->AddVertex(Vector3{ -1.f, -1.f, 0.f });
	deferredRenderingMesh->AddVertex(Vector3{ 3.f, -1.f, 0.f });
	deferredRenderingMesh->AddVertex(Vector3{ -1.f, 3.f, 0.f });
	deferredRenderingMesh->AddFace(Face{ 0, 1, 2 });
	deferredRenderingMesh->PreInit();

	deferredRenderingMeshShader = new Shader();
	deferredRenderingMeshShader->SetVertexShaderScript(ShaderBuilder::GetInstance()->GetVertexShaderScript_DeferredPass());
	deferredRenderingMeshShader->SetFragmentShaderScript(ShaderBuilder::GetInstance()->GetFragmentShaderScript_DeferredPass());

#if defined(GOKNAR_BUILD_DEBUG)
	IOManager::WriteFile((ContentDir + "DeferredRendering.vert").c_str(), deferredRenderingMeshShader->GetVertexShaderScript().c_str());
	IOManager::WriteFile((ContentDir + "DeferredRendering.frag").c_str(), deferredRenderingMeshShader->GetFragmentShaderScript().c_str());
#endif

	deferredRenderingMeshShader->PreInit();
	deferredRenderingMeshShader->Init();
	deferredRenderingMeshShader->PostInit();

	engine->GetRenderer()->BindShadowTextures(deferredRenderingMeshShader);

	geometryBufferData = new GeometryBufferData();
	geometryBufferData->Init();

	engine->GetWindowManager()->AddWindowSizeCallback(Delegate<void(int, int)>::create<DeferredRenderingData, &DeferredRenderingData::OnWindowSizeChange>(this));
}

void DeferredRenderingData::BindGeometryBuffer()
{
	geometryBufferData->Bind();
}

void DeferredRenderingData::UnbindGeometryBuffer()
{
	geometryBufferData->Unbind();
}

void DeferredRenderingData::Render()
{
	engine->GetRenderer()->BindStaticVBO();

	deferredRenderingMeshShader->Use();
	deferredRenderingMeshShader->SetInt(SHADER_VARIABLE_NAMES::GBUFFER::OUT_POSITION, geometryBufferData->worldPositionTexture->GetRendererTextureId());
	deferredRenderingMeshShader->SetInt(SHADER_VARIABLE_NAMES::GBUFFER::OUT_NORMAL, geometryBufferData->worldNormalTexture->GetRendererTextureId());
	deferredRenderingMeshShader->SetInt(SHADER_VARIABLE_NAMES::GBUFFER::OUT_DIFFUSE, geometryBufferData->diffuseTexture->GetRendererTextureId());
	deferredRenderingMeshShader->SetInt(SHADER_VARIABLE_NAMES::GBUFFER::OUT_SPECULAR_PHONG, geometryBufferData->specularTexture->GetRendererTextureId());

	engine->GetRenderer()->SetLightUniforms(deferredRenderingMeshShader);

	int facePointCount = deferredRenderingMesh->GetFaceCount() * 3;
	glDrawElementsBaseVertex(GL_TRIANGLES, facePointCount, GL_UNSIGNED_INT, (void*)(unsigned long long)deferredRenderingMesh->GetVertexStartingIndex(), deferredRenderingMesh->GetBaseVertex());
}

void DeferredRenderingData::OnWindowSizeChange(int x, int y)
{
	const std::vector<Material*>& materials = engine->GetResourceManager()->GetMaterials();

	decltype(materials.begin()) materialIteration = materials.begin();
	for (; materialIteration < materials.end(); ++materialIteration)
	{
		Shader* shader = (*materialIteration)->GetShader(RenderPassType::GeometryBuffer);
		if(shader)
		{
			BindGeometryBufferTextures(shader);
		}
	}
}

void DeferredRenderingData::BindGeometryBufferTextures(Shader* shader)
{
	shader->Use();
	
	geometryBufferData->worldPositionTexture->Bind(shader);
	geometryBufferData->worldNormalTexture->Bind(shader);
	geometryBufferData->diffuseTexture->Bind(shader);
	geometryBufferData->specularTexture->Bind(shader);
}

void DeferredRenderingData::BindGBufferDepth()
{
	geometryBufferData->BindGBufferDepth();
}