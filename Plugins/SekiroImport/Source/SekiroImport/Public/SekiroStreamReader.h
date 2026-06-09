#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

/// 流式JSON读取器：逐字符扫描大文件，按缓冲块读取
/// 将1GB动画JSON的内存峰值从~3GB降至~100MB
class SEKIROIMPORT_API FSekiroStreamReader
{
public:
    /// 每解析完一个动画片段就回调，返回false可提前终止
    DECLARE_DELEGATE_RetVal_OneParam(bool, FOnAnimationParsed, const FSekiroAnimationClip& /*Clip*/);

    /// 一步完成：解析骨架 + 流式解析动画
    /// @param FilePath JSON文件路径
    /// @param OutBones 输出的骨骼数组（146根含IK）
    /// @param Callback 每解析一个动画即回调
    /// @param MaxAnimations 最大动画数，0=全部
    /// @param NamePrefixFilter 动画名称前缀过滤（空=全部）
    /// @return 成功解析的动画数量
    static int32 ParseAll(const FString& FilePath, TArray<FSekiroImportBone>& OutBones, FOnAnimationParsed Callback, int32 MaxAnimations = 0, const FString& NamePrefixFilter = TEXT(""));

    /// 仅解析骨架（不解析动画）
    static bool ParseSkeleton(const FString& FilePath, TArray<FSekiroImportBone>& OutBones);

private:
    FSekiroStreamReader();
    ~FSekiroStreamReader();

    bool Open(const FString& FilePath);
    void Close();

    // ---- 缓冲管理 ----
    static constexpr int32 ChunkSize = 256 * 1024;
    TUniquePtr<FArchive> FileArchive;
    TArray<char> Buffer;
    int64 FileSize = 0;
    int64 BytesRead = 0;
    int32 BufferPos = 0;

    bool EnsureBuffer(int32 MinAvailable);
    char Peek() const { return (BufferPos < Buffer.Num()) ? Buffer[BufferPos] : '\0'; }
    void Advance() { ++BufferPos; }

    // ---- 扫描 ----
    void SkipWhitespace();

    // ---- 解析 ----
    bool ParseString(TArray<char>& OutChars);
    void SkipString();
    bool ParseInt(int32& OutValue);
    bool ParseFloat(float& OutValue);
    bool ParseFloatArray3(float& X, float& Y, float& Z);
    bool ParseFloatArray4(float& X, float& Y, float& Z, float& W);
    bool ParseTransformObj(FVector& OutPos, FQuat& OutRot, FVector& OutScale);
    void SkipFieldValue();
    int32 CaptureObjectText(FString& OutText);

    // ---- 高层流程 ----
    bool ParseRootSkeleton(TArray<FSekiroImportBone>& OutBones);
    int32 ParseAnimationsArray(const TArray<FSekiroImportBone>& SkeletonBones, FOnAnimationParsed Callback, int32 MaxAnimations, const FString& NamePrefixFilter);
    bool ParseOneAnimation(const TArray<FSekiroImportBone>& SkeletonBones, FSekiroAnimationClip& OutClip, const FString& NamePrefixFilter = TEXT(""));
};
