#include "pch.h"

#include "CameraManager.h"

#include "Goknar/Camera.h"
#include "Goknar/Engine.h"
#include "WindowManager.h"

CameraManager::~CameraManager()
{
	for (Camera* camera : cameras_)
	{
		delete camera;
	}
}

void CameraManager::PreInit()
{
	HandleNewlyAddedCameras();
}

void CameraManager::Init()
{
}

void CameraManager::PostInit()
{
}

void CameraManager::HandleNewlyAddedCameras()
{
	if(camerasToAddNextFrameCount_ == 0)
	{
		return;
	}

	std::vector<Camera*>::const_iterator cameraIterator = camerasToAddNextFrame_.cbegin();
	while(cameraIterator != camerasToAddNextFrame_.cend())
	{
		Camera* camera = *cameraIterator;

		camera->Init();

		cameras_.push_back(camera);

		if (activeCamera_ == nullptr && camera->GetCameraType() == CameraType::Scene)
		{
			SetActiveCamera(camera);
		}

		++cameraIterator;
	}

	camerasToAddNextFrame_.clear();
	camerasToAddNextFrameCount_ = 0;
}

void CameraManager::AddCamera(Camera* camera)
{
	camerasToAddNextFrame_.push_back(camera);
	++camerasToAddNextFrameCount_;
}

void CameraManager::SetActiveCamera(Camera* camera)
{
	activeCamera_ = camera;
	engine->GetWindowManager()->UpdateViewport();
}
