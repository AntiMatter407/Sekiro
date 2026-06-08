#include "Tools/USKAIToolRegistry.h"

void USKAIToolRegistry::RegisterTool(TScriptInterface<ISKAIToolInterface> Tool)
{
    if (!Tool)
    {
        return;
    }

    // 检查重名
    FName ToolName = Tool->GetToolName();
    for (int32 i = 0; i < Tools.Num(); ++i)
    {
        if (Tools[i] && Tools[i]->GetToolName() == ToolName)
        {
            Tools[i] = Tool;  // 覆盖
            return;
        }
    }

    Tools.Add(Tool);
}

void USKAIToolRegistry::UnregisterTool(const FName& ToolName)
{
    Tools.RemoveAll([&](const TScriptInterface<ISKAIToolInterface>& T)
    {
        return T && T->GetToolName() == ToolName;
    });
}

TArray<FName> USKAIToolRegistry::ListToolNames() const
{
    TArray<FName> Names;
    for (const auto& Tool : Tools)
    {
        if (Tool)
        {
            Names.Add(Tool->GetToolName());
        }
    }
    return Names;
}

TScriptInterface<ISKAIToolInterface> USKAIToolRegistry::FindTool(const FName& ToolName) const
{
    for (const auto& Tool : Tools)
    {
        if (Tool && Tool->GetToolName() == ToolName)
        {
            return Tool;
        }
    }
    return nullptr;
}
