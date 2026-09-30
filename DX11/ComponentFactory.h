#pragma once
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include "Component.h"

class ComponentFactory
{
    using CreateComponentFn = std::function<std::shared_ptr<Component>()>;

private:
    ComponentFactory();
    std::unordered_map<std::string, CreateComponentFn> m_FactoryMap;
    std::vector<HMODULE> m_LoadedLibraries;

public:
    static ComponentFactory& Instance();

    bool RegisterComponent(const std::string& type, CreateComponentFn fn);
    std::shared_ptr<Component> CreateComponent(const std::string& type);
    std::vector<std::string> GetComponentTypes() const;

private:
    void InitBuiltInComponents();
};