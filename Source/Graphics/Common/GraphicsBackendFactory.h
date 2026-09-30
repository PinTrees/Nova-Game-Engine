#pragma once
#include "IGraphicsBackend.h"
#include <memory>

class GraphicsBackendFactory
{
public:
	static std::unique_ptr<IGraphicsBackend> Create(GraphicsAPI api);
};
