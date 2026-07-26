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
