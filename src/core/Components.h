#pragma once

#include <string>

struct TransformComponent
{
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float scale[3]    = {1.0f, 1.0f, 1.0f};
};

struct TagComponent
{
    std::string tag;
};

struct MaterialComponent
{
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool active = true;
};

struct CameraComponent
{
    float fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    float pitch = 0.0f;   // degrees, about the camera's local right axis
    float yaw = 0.0f;     // degrees, about the world Y axis
    bool primary = false;
};
