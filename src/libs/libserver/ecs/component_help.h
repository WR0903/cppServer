#pragma once

#include "ecs/entity_system.h"
#include "utils/yaml.h"
#include "utils/res_path.h"
#include "utils/trace_component.h"

class ComponentHelp
{
public:
    static EntitySystem* GetGlobalEntitySystem();
    static Yaml* GetYaml();
    static ResPath* GetResPath();
    static TraceComponent* GetTraceComponent();
    static void CatchError(bool bResult); // 打印当前堆栈
};
