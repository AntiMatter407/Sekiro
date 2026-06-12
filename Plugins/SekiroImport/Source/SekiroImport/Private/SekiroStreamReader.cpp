#include "SekiroStreamReader.h"
#include "SekiroAnimationNameMap.h"
#include "SekiroImportLog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"

/// 将ASCII字符数组转换为FString（字段名均为ASCII）
static FString CharArrayToFString(const TArray<char>& Chars)
{
    FString Result;
    Result.Reserve(Chars.Num());
    for (char C : Chars) Result.AppendChar((TCHAR)C);
    return Result;
}

// ============================================================================
// 静态入口
// ============================================================================

int32 FSekiroStreamReader::ParseAll(const FString& FilePath, TArray<FSekiroImportBone>& OutBones, FOnAnimationParsed Callback, int32 MaxAnimations, const FString& NamePrefixFilter)
{
    FSekiroStreamReader Reader;
    if (!Reader.Open(FilePath)) return 0;
    if (!Reader.ParseRootSkeleton(OutBones)) return 0;
    int32 Count = Reader.ParseAnimationsArray(OutBones, Callback, MaxAnimations, NamePrefixFilter);
    Reader.Close();
    return Count;
}

bool FSekiroStreamReader::ParseSkeleton(const FString& FilePath, TArray<FSekiroImportBone>& OutBones)
{
    FSekiroStreamReader Reader;
    if (!Reader.Open(FilePath)) return false;
    bool bOK = Reader.ParseRootSkeleton(OutBones);
    Reader.Close();
    return bOK;
}

// ============================================================================
// 构造/析构
// ============================================================================

FSekiroStreamReader::FSekiroStreamReader()
{
    Buffer.Reserve(ChunkSize * 2);
}

FSekiroStreamReader::~FSekiroStreamReader()
{
    Close();
}

// ============================================================================
// 文件操作
// ============================================================================

bool FSekiroStreamReader::Open(const FString& FilePath)
{
    FileArchive.Reset(IFileManager::Get().CreateFileReader(*FilePath, FILEREAD_AllowWrite));
    if (!FileArchive)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("流式读取器无法打开: %s"), *FilePath);
        return false;
    }
    FileSize = FileArchive->TotalSize();
    Buffer.Reset();
    BufferPos = 0;
    BytesRead = 0;
    UE_LOG(LogSekiroImport, Log, TEXT("流式读取器已打开: %.1f MB"), FileSize / (1024.0f * 1024.0f));
    return true;
}

void FSekiroStreamReader::Close()
{
    FileArchive.Reset();
    Buffer.Empty();
    FileSize = 0;
    BytesRead = 0;
    BufferPos = 0;
}

// ============================================================================
// 缓冲管理
// ============================================================================

bool FSekiroStreamReader::EnsureBuffer(int32 MinAvailable)
{
    if (Buffer.Num() - BufferPos >= MinAvailable) return true;

    // 保留未处理数据
    int32 Remaining = Buffer.Num() - BufferPos;
    if (Remaining > 0 && BufferPos > 0)
    {
        FMemory::Memmove(Buffer.GetData(), Buffer.GetData() + BufferPos, Remaining);
    }
    Buffer.SetNum(Remaining, false);
    BufferPos = 0;

    // 读取下一块
    int32 ToRead = FMath::Min(ChunkSize, (int32)(FileSize - BytesRead));
    if (ToRead <= 0) return false;

    int32 OldSize = Buffer.Num();
    Buffer.AddUninitialized(ToRead);
    FileArchive->Serialize(Buffer.GetData() + OldSize, ToRead);
    BytesRead += ToRead;

    return Buffer.Num() - BufferPos >= MinAvailable;
}

// ============================================================================
// 空白与跳过
// ============================================================================

void FSekiroStreamReader::SkipWhitespace()
{
    while (EnsureBuffer(1))
    {
        char C = Peek();
        if (C == ' ' || C == '\t' || C == '\r' || C == '\n') { Advance(); continue; }
        break;
    }
}

// ============================================================================
// 字符串解析
// ============================================================================

bool FSekiroStreamReader::ParseString(TArray<char>& OutChars)
{
    OutChars.Reset();
    if (!EnsureBuffer(1) || Peek() != '"') return false;
    Advance();

    while (EnsureBuffer(1))
    {
        char C = Peek();
        if (C == '\\') { Advance(); if (EnsureBuffer(1)) { OutChars.Add(Peek()); Advance(); } continue; }
        if (C == '"') { Advance(); return true; }
        OutChars.Add(C);
        Advance();
    }
    return false;
}

void FSekiroStreamReader::SkipString()
{
    if (!EnsureBuffer(1) || Peek() != '"') return;
    Advance();
    while (EnsureBuffer(1))
    {
        char C = Peek();
        if (C == '\\') { Advance(); if (EnsureBuffer(1)) Advance(); continue; }
        if (C == '"') { Advance(); return; }
        Advance();
    }
}

// ============================================================================
// 数字解析
// ============================================================================

bool FSekiroStreamReader::ParseInt(int32& OutValue)
{
    SkipWhitespace();
    if (!EnsureBuffer(1)) return false;

    bool bNeg = (Peek() == '-');
    if (bNeg) Advance();

    OutValue = 0;
    bool bHasDigit = false;
    while (EnsureBuffer(1) && Peek() >= '0' && Peek() <= '9')
    {
        OutValue = OutValue * 10 + (Peek() - '0');
        bHasDigit = true;
        Advance();
    }
    if (bNeg) OutValue = -OutValue;
    return bHasDigit;
}

bool FSekiroStreamReader::ParseFloat(float& OutValue)
{
    SkipWhitespace();
    if (!EnsureBuffer(32)) return false;

    char NumBuf[64];
    int32 Len = 0;

    if (Peek() == '-') { NumBuf[Len++] = '-'; Advance(); }

    while (EnsureBuffer(1) && Len < 60)
    {
        char C = Peek();
        if ((C >= '0' && C <= '9') || C == '.' || C == 'e' || C == 'E')
        {
            NumBuf[Len++] = C; Advance();
        }
        else if ((C == '+' || C == '-') && Len > 0 && (NumBuf[Len-1] == 'e' || NumBuf[Len-1] == 'E'))
        {
            NumBuf[Len++] = C; Advance();
        }
        else break;
    }
    if (Len == 0) return false;
    NumBuf[Len] = '\0';
    OutValue = FCStringAnsi::Atof(NumBuf);
    return true;
}

// ============================================================================
// 数组解析
// ============================================================================

bool FSekiroStreamReader::ParseFloatArray3(float& X, float& Y, float& Z)
{
    SkipWhitespace();
    if (!EnsureBuffer(1) || Peek() != '[') return false;
    Advance();
    ParseFloat(X);
    SkipWhitespace(); if (Peek() == ',') Advance();
    ParseFloat(Y);
    SkipWhitespace(); if (Peek() == ',') Advance();
    ParseFloat(Z);
    SkipWhitespace();
    if (Peek() == ']') { Advance(); return true; }
    return false;
}

bool FSekiroStreamReader::ParseFloatArray4(float& X, float& Y, float& Z, float& W)
{
    SkipWhitespace();
    if (!EnsureBuffer(1) || Peek() != '[') return false;
    Advance();
    ParseFloat(X);
    SkipWhitespace(); if (Peek() == ',') Advance();
    ParseFloat(Y);
    SkipWhitespace(); if (Peek() == ',') Advance();
    ParseFloat(Z);
    SkipWhitespace(); if (Peek() == ',') Advance();
    ParseFloat(W);
    SkipWhitespace();
    if (Peek() == ']') { Advance(); return true; }
    return false;
}

// ============================================================================
// 变换对象解析
// ============================================================================

bool FSekiroStreamReader::ParseTransformObj(FVector& OutPos, FQuat& OutRot, FVector& OutScale)
{
    SkipWhitespace();
    if (!EnsureBuffer(1) || Peek() != '{') return false;
    Advance();

    while (EnsureBuffer(1))
    {
        SkipWhitespace();
        if (Peek() == '}') { Advance(); return true; }
        if (Peek() == ',') { Advance(); continue; }

        if (Peek() == '"')
        {
            TArray<char> FName;
            if (!ParseString(FName)) return false;
            SkipWhitespace();
            if (Peek() == ':') Advance();

            if (FName.Num() == 1)
            {
                if (FName[0] == 'P')
                {
                    float X, Y, Z;
                    if (ParseFloatArray3(X, Y, Z))
                        OutPos = FVector(X * 100.0f, Y * 100.0f, Z * 100.0f); // m→cm, 保持Y-up
                }
                else if (FName[0] == 'R')
                {
                    float X, Y, Z, W;
                    if (ParseFloatArray4(X, Y, Z, W))
                        OutRot = FQuat(X, Y, Z, W); // 四元数不变, 保持Y-up
                }
                else if (FName[0] == 'S')
                {
                    float X, Y, Z;
                    if (ParseFloatArray3(X, Y, Z))
                        OutScale = FVector(X, Y, Z);
                }
                else SkipFieldValue();
            }
            else SkipFieldValue();
        }
    }
    return false;
}

// ============================================================================
// 跳过值
// ============================================================================

void FSekiroStreamReader::SkipFieldValue()
{
    SkipWhitespace();
    if (!EnsureBuffer(1)) return;

    char C = Peek();
    if (C == '"') { SkipString(); return; }
    if (C == '{' || C == '[')
    {
        char Open = C, Close = (C == '{') ? '}' : ']';
        int32 Depth = 1;
        Advance();
        bool bInStr = false, bEsc = false;
        while (Depth > 0 && EnsureBuffer(1))
        {
            C = Peek();
            if (bEsc) { bEsc = false; Advance(); continue; }
            if (C == '\\' && bInStr) { bEsc = true; Advance(); continue; }
            if (C == '"') { bInStr = !bInStr; Advance(); continue; }
            if (!bInStr)
            {
                if (C == Open) ++Depth;
                if (C == Close) --Depth;
            }
            Advance();
        }
        return;
    }
    // 数字/字面量
    while (EnsureBuffer(1))
    {
        C = Peek();
        if (C == ',' || C == '}' || C == ']' || C == ' ' || C == '\t' || C == '\r' || C == '\n') break;
        Advance();
    }
}

// ============================================================================
// 对象文本捕获
// ============================================================================

int32 FSekiroStreamReader::CaptureObjectText(FString& OutText)
{
    SkipWhitespace();
    if (!EnsureBuffer(1) || Peek() != '{') return 0;
    Advance();

    TArray<TCHAR> Chars;
    Chars.Add('{');

    int32 Depth = 1;
    bool bInStr = false, bEsc = false;

    while (Depth > 0 && EnsureBuffer(1))
    {
        char C = Peek();
        Chars.Add(C);

        if (bEsc) { bEsc = false; Advance(); continue; }
        if (C == '\\' && bInStr) { bEsc = true; Advance(); continue; }
        if (C == '"') { bInStr = !bInStr; Advance(); continue; }
        if (!bInStr)
        {
            if (C == '{') ++Depth;
            if (C == '}') --Depth;
        }
        Advance();
    }

    Chars.Add('\0');
    OutText = FString(Chars.Num() - 1, Chars.GetData());
    return Chars.Num() - 1;
}

// ============================================================================
// 根对象骨架解析
// ============================================================================

bool FSekiroStreamReader::ParseRootSkeleton(TArray<FSekiroImportBone>& OutBones)
{
    SkipWhitespace();
    if (!EnsureBuffer(1) || Peek() != '{')
    {
        UE_LOG(LogSekiroImport, Error, TEXT("JSON必须以'{'开头"));
        return false;
    }
    Advance();

    int32 BoneCount = 0;
    TArray<FString> BoneNamesRaw;
    TArray<int32> BoneParents;
    TArray<FTransform> BoneTransforms;

    bool bParsingTransforms = false;

    while (EnsureBuffer(1))
    {
        SkipWhitespace();
        char C = Peek();
        if (C == '}') { Advance(); break; }
        if (C == ',') { Advance(); continue; }

        if (C == '"')
        {
            TArray<char> FName;
            if (!ParseString(FName)) break;
            SkipWhitespace();
            if (Peek() == ':') Advance();
            SkipWhitespace();

            // 将字段名转为FString比较
            FString Field = CharArrayToFString(FName);

            if (Field == TEXT("BoneCount"))
            {
                ParseInt(BoneCount);
            }
            else if (Field == TEXT("BoneNames"))
            {
                if (Peek() == '[')
                {
                    Advance();
                    BoneNamesRaw.Reserve(BoneCount > 0 ? BoneCount : 200);
                    while (EnsureBuffer(1))
                    {
                        SkipWhitespace();
                        if (Peek() == ']') { Advance(); break; }
                        if (Peek() == ',') { Advance(); continue; }
                        TArray<char> Name;
                        if (ParseString(Name))
                            BoneNamesRaw.Add(CharArrayToFString(Name));
                    }
                }
            }
            else if (Field == TEXT("BoneParents"))
            {
                if (Peek() == '[')
                {
                    Advance();
                    BoneParents.Reserve(BoneNamesRaw.Num());
                    while (EnsureBuffer(1))
                    {
                        SkipWhitespace();
                        if (Peek() == ']') { Advance(); break; }
                        if (Peek() == ',') { Advance(); continue; }
                        int32 Val;
                        if (ParseInt(Val)) BoneParents.Add(Val);
                        else Advance();
                    }
                }
            }
            else if (Field == TEXT("BoneLocalTransforms"))
            {
                if (Peek() == '[')
                {
                    Advance();
                    bParsingTransforms = true;
                    BoneTransforms.Reserve(BoneNamesRaw.Num());

                    while (EnsureBuffer(1))
                    {
                        SkipWhitespace();
                        if (Peek() == ']') { Advance(); break; }
                        if (Peek() == ',') { Advance(); continue; }

                        FVector Pos(0, 0, 0);
                        FQuat Rot = FQuat::Identity;
                        FVector Scale(1, 1, 1);
                        if (ParseTransformObj(Pos, Rot, Scale))
                            BoneTransforms.Add(FTransform(Rot, Pos, Scale));
                        else
                            Advance();
                    }
                    bParsingTransforms = false;
                }
                // BoneLocalTransforms是骨架数据的最后一个字段，解析完成后退出
                break;
            }
            else
            {
                // 其他字段跳过（AnimationCount等）
                SkipFieldValue();
            }
        }
        else
        {
            Advance();
        }
    }

    // 构建骨骼数组
    int32 N = FMath::Min3(BoneNamesRaw.Num(), BoneParents.Num(), BoneTransforms.Num());
    OutBones.Reserve(N);
    for (int32 i = 0; i < N; ++i)
    {
        FSekiroImportBone Bone;
        Bone.Name = FName(*BoneNamesRaw[i]);
        Bone.ParentIndex = (i < BoneParents.Num()) ? BoneParents[i] : INDEX_NONE;
        Bone.LocalTranslation = BoneTransforms[i].GetTranslation();
        Bone.LocalRotation = BoneTransforms[i].GetRotation();
        Bone.LocalScale = BoneTransforms[i].GetScale3D();
        OutBones.Add(MoveTemp(Bone));
    }

    UE_LOG(LogSekiroImport, Log, TEXT("流式骨架解析完成: %d 根骨骼"), OutBones.Num());
    return OutBones.Num() > 0;
}

// ============================================================================
// 流式动画解析
// ============================================================================

int32 FSekiroStreamReader::ParseAnimationsArray(const TArray<FSekiroImportBone>& SkeletonBones, FOnAnimationParsed Callback, int32 MaxAnimations, const FString& NamePrefixFilter)
{
    // 当前位置在BoneLocalTransforms数组']'之后
    // 继续扫描根对象字段，寻找"Animations"
    while (EnsureBuffer(1))
    {
        SkipWhitespace();
        char C = Peek();
        if (C == '}') { Advance(); break; }
        if (C == ',') { Advance(); continue; }

        if (C == '"')
        {
            TArray<char> FName;
            if (!ParseString(FName)) break;
            SkipWhitespace();
            if (Peek() == ':') Advance();

            FString Field = CharArrayToFString(FName);

            if (Field == TEXT("Animations"))
            {
                SkipWhitespace();
                if (!EnsureBuffer(1) || Peek() != '[')
                {
                    UE_LOG(LogSekiroImport, Error, TEXT("期望Animations数组的'['"));
                    return 0;
                }
                Advance(); // 跳过 '['

                int32 ParsedCount = 0;
                while (EnsureBuffer(1))
                {
                    SkipWhitespace();
                    if (Peek() == ']') { Advance(); break; }
                    if (Peek() == ',') { Advance(); continue; }

                    if (MaxAnimations > 0 && ParsedCount >= MaxAnimations) break;

                    FSekiroAnimationClip Clip;
                    if (ParseOneAnimation(SkeletonBones, Clip, NamePrefixFilter))
                    {
                        if (!Callback.Execute(Clip))
                            break; // 回调返回false，停止解析
                        ++ParsedCount;
                    }
                }

                UE_LOG(LogSekiroImport, Log, TEXT("流式动画解析完成: %d 个"), ParsedCount);
                return ParsedCount;
            }
            else
            {
                SkipFieldValue();
            }
        }
        else
        {
            Advance();
        }
    }

    UE_LOG(LogSekiroImport, Warning, TEXT("未找到Animations字段"));
    return 0;
}

// ============================================================================
// 单个动画解析（捕获文本 → FJsonSerializer → FSekiroAnimationClip）
// ============================================================================

bool FSekiroStreamReader::ParseOneAnimation(const TArray<FSekiroImportBone>& SkeletonBones, FSekiroAnimationClip& OutClip, const FString& NamePrefixFilter)
{
    FString AnimText;
    int32 Len = CaptureObjectText(AnimText);
    if (Len == 0) return false;

    // 快速名称过滤：在原始JSON文本中查找Name字段（避免FJsonSerializer解析帧数据）
    if (!NamePrefixFilter.IsEmpty())
    {
        int32 NameIdx = AnimText.Find(TEXT("\"Name\":\""));
        if (NameIdx != INDEX_NONE)
        {
            NameIdx += 8; // 跳过 "Name":"
            int32 NameEnd = AnimText.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, NameIdx);
            if (NameEnd != INDEX_NONE)
            {
                FString AnimName = AnimText.Mid(NameIdx, NameEnd - NameIdx);
                if (!AnimName.StartsWith(NamePrefixFilter))
                {
                    return false; // 名称不匹配，跳过（已避免昂贵的JSON解析）
                }
            }
        }
    }

    TSharedPtr<FJsonObject> AnimObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(AnimText);

    if (!FJsonSerializer::Deserialize(Reader, AnimObj) || !AnimObj.IsValid())
        return false;

    OutClip.Name = AnimObj->GetStringField(TEXT("Name"));
    OutClip.Name = *FSekiroAnimationNameMap::Translate(OutClip.Name);
    OutClip.Duration = (float)AnimObj->GetNumberField(TEXT("Duration"));
    OutClip.FrameCount = (int32)AnimObj->GetNumberField(TEXT("FrameCount"));
    OutClip.SampleRate = (float)AnimObj->GetNumberField(TEXT("SampleRate"));

    const int32 BoneCount = SkeletonBones.Num();
    OutClip.BoneNames.Reserve(BoneCount);
    for (const FSekiroImportBone& B : SkeletonBones)
        OutClip.BoneNames.Add(B.Name);

    // 复制参考姿态Local变换到Clip
    OutClip.ReferenceLocalTransforms.Reserve(BoneCount);
    for (const FSekiroImportBone& B : SkeletonBones)
        OutClip.ReferenceLocalTransforms.Add(FTransform(B.LocalRotation, B.LocalTranslation, B.LocalScale));

    const TArray<TSharedPtr<FJsonValue>>* FramesArr = nullptr;
    if (!AnimObj->TryGetArrayField(TEXT("Frames"), FramesArr))
        return false;

    OutClip.FrameData.Reserve(FramesArr->Num());

    for (const TSharedPtr<FJsonValue>& FrameVal : *FramesArr)
    {
        const TSharedPtr<FJsonObject>* FrameObj = nullptr;
        if (!FrameVal->TryGetObject(FrameObj)) continue;

        const TArray<TSharedPtr<FJsonValue>>* BoneXforms = nullptr;
        if (!(*FrameObj)->TryGetArrayField(TEXT("BoneTransforms"), BoneXforms)) continue;

        TArray<FTransform> Frame;
        Frame.Reserve(BoneCount);

        for (int32 b = 0; b < FMath::Min(BoneXforms->Num(), BoneCount); ++b)
        {
            const TSharedPtr<FJsonObject>* XfObj = nullptr;
            if (!(*BoneXforms)[b]->TryGetObject(XfObj))
            {
                Frame.Add(FTransform::Identity);
                continue;
            }

            FVector Pos(0, 0, 0);
            FQuat Rot = FQuat::Identity;
            FVector Scale(1, 1, 1);

            const TArray<TSharedPtr<FJsonValue>>* P;
            if ((*XfObj)->TryGetArrayField(TEXT("P"), P) && P->Num() >= 3)
                Pos = FVector((float)(*P)[0]->AsNumber() * 100.0f, (float)(*P)[1]->AsNumber() * 100.0f, (float)(*P)[2]->AsNumber() * 100.0f); // m→cm, 保持Y-up

            const TArray<TSharedPtr<FJsonValue>>* R;
            if ((*XfObj)->TryGetArrayField(TEXT("R"), R) && R->Num() >= 4)
                Rot = FQuat((float)(*R)[0]->AsNumber(), (float)(*R)[1]->AsNumber(), (float)(*R)[2]->AsNumber(), (float)(*R)[3]->AsNumber()); // 四元数不变, 保持Y-up

            const TArray<TSharedPtr<FJsonValue>>* S;
            if ((*XfObj)->TryGetArrayField(TEXT("S"), S) && S->Num() >= 3)
                Scale = FVector((float)(*S)[0]->AsNumber(), (float)(*S)[1]->AsNumber(), (float)(*S)[2]->AsNumber());

            Frame.Add(FTransform(Rot, Pos, Scale));
        }

        while (Frame.Num() < BoneCount)
            Frame.Add(FTransform::Identity);

        OutClip.FrameData.Add(MoveTemp(Frame));
    }

    return OutClip.FrameData.Num() > 0;
}
