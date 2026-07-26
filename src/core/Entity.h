#pragma once

#include "Components.h"

struct Entity
{
    int id;
    TagComponent tag;
    TransformComponent transform;
    MaterialComponent material;
};
