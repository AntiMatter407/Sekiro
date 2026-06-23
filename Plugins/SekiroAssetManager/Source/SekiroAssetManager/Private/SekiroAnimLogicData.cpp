#include "SekiroAnimLogicData.h"

FString USKAnimationLogicData::BuildAnimAssetPath(int32 AnimID) const
{
    FString Prefix = AnimPrefixMap.FindRef(AnimID);
    if (Prefix.IsEmpty()) Prefix = TEXT("a000");

    FString PackageName = FString::Printf(TEXT("%s/%s_%s_%06d"),
        *AnimAssetBasePath, *AnimAssetNamePrefix, *Prefix, AnimID);
    FString ObjectName  = FString::Printf(TEXT("%s_%s_%06d"),
        *AnimAssetNamePrefix, *Prefix, AnimID);
    return PackageName + TEXT(".") + ObjectName;
}
