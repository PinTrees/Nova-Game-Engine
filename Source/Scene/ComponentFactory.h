#pragma once
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include "Component.h"

class NOVA_API ComponentFactory
{
    using CreateComponentFn = std::function<std::shared_ptr<Component>()>;

private:
    ComponentFactory();
    std::unordered_map<std::string, CreateComponentFn> m_FactoryMap;
    std::vector<HMODULE> m_LoadedLibraries;

public:
    static ComponentFactory& Instance();

    bool RegisterComponent(const std::string& type, CreateComponentFn fn);
    void UnregisterComponent(const std::string& type);   // 패키지 DLL 을 내릴 때
    std::shared_ptr<Component> CreateComponent(const std::string& type);
    std::vector<std::string> GetComponentTypes() const;

private:
    void InitBuiltInComponents();
};