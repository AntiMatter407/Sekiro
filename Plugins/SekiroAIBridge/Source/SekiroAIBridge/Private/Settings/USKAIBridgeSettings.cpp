#include "Settings/USKAIBridgeSettings.h"

USKAIBridgeSettings::USKAIBridgeSettings()
    : ServerPort(9877)
    , bAutoStartServer(true)
    , BindAddress(TEXT("127.0.0.1"))
    , PreSharedKey(TEXT(""))
    , ConfirmationPolicy(TEXT("Session"))
    , bReadOnlyMode(false)
{
}

USKAIBridgeSettings* USKAIBridgeSettings::Get()
{
    return GetMutableDefault<USKAIBridgeSettings>();
}

FName USKAIBridgeSettings::GetCategoryName() const
{
    return FName(TEXT("Plugins"));
}

#if WITH_EDITOR
FText USKAIBridgeSettings::GetSectionText() const
{
    return FText::FromString(TEXT("Sekiro AI Bridge"));
}
#endif
