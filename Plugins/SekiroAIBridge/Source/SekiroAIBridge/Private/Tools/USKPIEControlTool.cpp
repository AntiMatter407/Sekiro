#include "Tools/USKPIEControlTool.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "PlayInEditorDataTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

FString USKPIEControlTool::GetToolDescription() const
{
	return TEXT("PIE（Play In Editor）控制：启动/停止/暂停/恢复 PIE 会话，支持多种模式和网络配置。");
}

FString USKPIEControlTool::GetInputSchemaJson() const
{
	return TEXT(
		"{"
		"\"type\":\"object\","
		"\"properties\":{"
			"\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"stop\",\"pause\",\"resume\",\"status\",\"late_join\"],\"description\":\"PIE action\"},"
			"\"mode\":{\"type\":\"string\",\"enum\":[\"selected\",\"standalone\",\"mobile\",\"vulkan\",\"vr\",\"simulate\"],\"description\":\"PIE mode (only for start)\"},"
			"\"clients\":{\"type\":\"integer\",\"description\":\"Number of clients (only for start)\"},"
			"\"net_mode\":{\"type\":\"string\",\"enum\":[\"standalone\",\"listen\",\"client\"],\"description\":\"Net mode (only for start)\"},"
			"\"viewport\":{\"type\":\"boolean\",\"description\":\"Use in-viewport instead of new window (only for start)\"}"
		"},"
		"\"required\":[\"action\"]"
		"}"
	);
}

FString USKPIEControlTool::GetConfirmationSummary(const FString& ArgsJson) const
{
	TSharedPtr<FJsonObject> Args;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
	if (FJsonSerializer::Deserialize(Reader, Args) && Args.IsValid())
	{
		FString Action = Args->GetStringField(TEXT("action"));
		if (Action == TEXT("start"))
		{
			FString Mode;
			Args->TryGetStringField(TEXT("mode"), Mode);
			if (Mode.IsEmpty()) Mode = TEXT("selected");
			return FString::Printf(TEXT("启动 PIE（模式: %s）"), *Mode);
		}
		if (Action == TEXT("stop"))  return TEXT("停止 PIE 会话");
		if (Action == TEXT("pause")) return TEXT("暂停 PIE 会话");
		if (Action == TEXT("resume")) return TEXT("恢复 PIE 会话");
	}
	return TEXT("PIE 控制操作");
}

// ============================================================================
// Execute — 主路由
// ============================================================================

FString USKPIEControlTool::Execute(const FString& ArgsJson, FString& OutError)
{
	TSharedPtr<FJsonObject> Args;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
	if (!FJsonSerializer::Deserialize(Reader, Args) || !Args.IsValid())
	{
		Args = MakeShareable(new FJsonObject());
	}

	FString Action;
	if (!Args->TryGetStringField(TEXT("action"), Action))
	{
		Action = TEXT("status");
	}

	if (Action == TEXT("start"))     return HandleStart(Args, OutError);
	if (Action == TEXT("stop"))      return HandleStop(OutError);
	if (Action == TEXT("pause"))     return HandlePause(OutError);
	if (Action == TEXT("resume"))    return HandleResume(OutError);
	if (Action == TEXT("status"))    return HandleStatus();
	if (Action == TEXT("late_join")) return HandleLateJoin(OutError);

	OutError = FString::Printf(TEXT("未知 PIE action: %s，支持: start, stop, pause, resume, status, late_join"), *Action);
	return FString();
}

// ============================================================================
// Start
// ============================================================================

FString USKPIEControlTool::HandleStart(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor 不可用");
		return FString();
	}

	if (GEditor->IsPlaySessionInProgress())
	{
		OutError = TEXT("PIE 已在运行中，请先停止当前会话");
		return FString();
	}

	FRequestPlaySessionParams Params;

	// 解析 mode
	FString Mode;
	Args->TryGetStringField(TEXT("mode"), Mode);

	if (Mode == TEXT("standalone"))
	{
		Params.SessionDestination = EPlaySessionDestinationType::NewProcess;
		FString CmdLine;
		Args->TryGetStringField(TEXT("cmd_line"), CmdLine);
		if (!CmdLine.IsEmpty())
		{
			Params.AdditionalStandaloneCommandLineParameters = CmdLine;
		}
	}
	else if (Mode == TEXT("simulate"))
	{
		Params.WorldType = EPlaySessionWorldType::SimulateInEditor;
	}
	else if (Mode == TEXT("mobile"))
	{
		Params.SessionPreviewTypeOverride = EPlaySessionPreviewType::MobilePreview;
	}
	else if (Mode == TEXT("vulkan"))
	{
		Params.SessionPreviewTypeOverride = EPlaySessionPreviewType::VulkanPreview;
	}
	else if (Mode == TEXT("vr"))
	{
		Params.SessionPreviewTypeOverride = EPlaySessionPreviewType::VRPreview;
	}
	else
	{
		// selected (default) — 当前视口 PIE
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		Params.SessionDestination = EPlaySessionDestinationType::InProcess;

		// 是否使用视口内运行
		bool bUseViewport = true;
		Args->TryGetBoolField(TEXT("viewport"), bUseViewport);
		if (!bUseViewport)
		{
			Params.SessionDestination = EPlaySessionDestinationType::InProcess;
		}
	}

	// 解析 网络配置
	ULevelEditorPlaySettings* PlaySettings = NewObject<ULevelEditorPlaySettings>();

	int32 Clients = 1;
	Args->TryGetNumberField(TEXT("clients"), Clients);
	PlaySettings->SetPlayNumberOfClients(FMath::Clamp(Clients, 1, 64));

	FString NetModeStr;
	Args->TryGetStringField(TEXT("net_mode"), NetModeStr);

	EPlayNetMode PlayNetMode = EPlayNetMode::PIE_Standalone;

	if (NetModeStr == TEXT("listen"))
	{
		PlayNetMode = EPlayNetMode::PIE_ListenServer;
	}
	else if (NetModeStr == TEXT("client"))
	{
		PlayNetMode = EPlayNetMode::PIE_Client;
	}

	// 检查 legacy 布尔标志
	bool bDedicated = false;
	bool bListen = false;
	if (Args->TryGetBoolField(TEXT("dedicated"), bDedicated) && bDedicated)
	{
		PlayNetMode = EPlayNetMode::PIE_Client;
	}
	if (Args->TryGetBoolField(TEXT("listen"), bListen) && bListen)
	{
		PlayNetMode = EPlayNetMode::PIE_ListenServer;
	}
	bool bServer = false;
	if (Args->TryGetBoolField(TEXT("server"), bServer) && bServer)
	{
		PlayNetMode = EPlayNetMode::PIE_ListenServer;
	}

	PlaySettings->SetPlayNetMode(PlayNetMode);
	Params.EditorPlaySettings = PlaySettings;

	GEditor->RequestPlaySession(Params);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("PIE 启动中（模式: %s, 客户端: %d, 网络: %s）"),
		Mode.IsEmpty() ? TEXT("selected") : *Mode,
		Clients,
		NetModeStr.IsEmpty() ? TEXT("standalone") : *NetModeStr));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}

// ============================================================================
// Stop
// ============================================================================

FString USKPIEControlTool::HandleStop(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor 不可用");
		return FString();
	}

	if (!GEditor->IsPlaySessionInProgress())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("PIE 未在运行"));

		FString Output;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
		return Output;
	}

	GEditor->RequestEndPlayMap();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("PIE 停止请求已发送"));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}

// ============================================================================
// Pause
// ============================================================================

FString USKPIEControlTool::HandlePause(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor 不可用");
		return FString();
	}

	if (!GEditor->IsPlayingSessionInEditor())
	{
		OutError = TEXT("PIE 未运行，无法暂停");
		return FString();
	}

	if (GEditor->PlayWorld && GEditor->PlayWorld->bDebugPauseExecution)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("PIE 已处于暂停状态"));

		FString Output;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
		return Output;
	}

	bool bPaused = GEditor->SetPIEWorldsPaused(true);
	if (bPaused)
	{
		GEditor->PlaySessionPaused();
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), bPaused);
	Result->SetStringField(TEXT("message"), bPaused ? TEXT("PIE 已暂停") : TEXT("暂停失败"));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}

// ============================================================================
// Resume
// ============================================================================

FString USKPIEControlTool::HandleResume(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor 不可用");
		return FString();
	}

	if (!GEditor->IsPlayingSessionInEditor())
	{
		OutError = TEXT("PIE 未运行，无法恢复");
		return FString();
	}

	if (!(GEditor->PlayWorld && GEditor->PlayWorld->bDebugPauseExecution))
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("PIE 未暂停，无需恢复"));

		FString Output;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
		return Output;
	}

	bool bResumed = GEditor->SetPIEWorldsPaused(false);
	if (bResumed)
	{
		GEditor->PlaySessionResumed();
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), bResumed);
	Result->SetStringField(TEXT("message"), bResumed ? TEXT("PIE 已恢复") : TEXT("恢复失败"));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}

// ============================================================================
// Status
// ============================================================================

FString USKPIEControlTool::HandleStatus()
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());

	if (GEditor)
	{
		const bool bInProgress = GEditor->IsPlaySessionInProgress();
		const bool bPlaying = GEditor->IsPlayingSessionInEditor();
		const bool bSimulating = GEditor->IsSimulatingInEditor();
		const bool bQueued = GEditor->IsPlaySessionRequestQueued();
		const bool bPaused = bPlaying && GEditor->PlayWorld && GEditor->PlayWorld->bDebugPauseExecution;

		Result->SetBoolField(TEXT("inProgress"), bInProgress);
		Result->SetBoolField(TEXT("playing"), bPlaying);
		Result->SetBoolField(TEXT("simulating"), bSimulating);
		Result->SetBoolField(TEXT("queued"), bQueued);
		Result->SetBoolField(TEXT("paused"), bPaused);

		// 从当前会话获取详细信息
		if (bPlaying)
		{
			const TOptional<FPlayInEditorSessionInfo> SessionInfo = GEditor->GetPlayInEditorSessionInfo();
			if (SessionInfo.IsSet())
			{
				const FRequestPlaySessionParams& P = SessionInfo->OriginalRequestParams;
				Result->SetStringField(TEXT("worldType"),
					P.WorldType == EPlaySessionWorldType::PlayInEditor ? TEXT("PlayInEditor") :
					P.WorldType == EPlaySessionWorldType::SimulateInEditor ? TEXT("SimulateInEditor") : TEXT("Unknown"));
			}
		}

		if (const TOptional<FRequestPlaySessionParams> Request = GEditor->GetPlaySessionRequest())
		{
			Result->SetStringField(TEXT("queuedType"),
				Request->WorldType == EPlaySessionWorldType::PlayInEditor ? TEXT("PlayInEditor") :
				Request->WorldType == EPlaySessionWorldType::SimulateInEditor ? TEXT("SimulateInEditor") : TEXT("Unknown"));
		}

		if (bPlaying && GEditor->PlayWorld)
		{
			const ENetMode NetMode = GEditor->PlayWorld->GetNetMode();
			Result->SetStringField(TEXT("netMode"),
				NetMode == NM_Standalone ? TEXT("Standalone") :
				NetMode == NM_DedicatedServer ? TEXT("DedicatedServer") :
				NetMode == NM_ListenServer ? TEXT("ListenServer") :
				NetMode == NM_Client ? TEXT("Client") : TEXT("Unknown"));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("inProgress"), false);
		Result->SetStringField(TEXT("error"), TEXT("GEditor unavailable"));
	}

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}

// ============================================================================
// Late Join
// ============================================================================

FString USKPIEControlTool::HandleLateJoin(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor 不可用");
		return FString();
	}

	if (!GEditor->IsPlayingSessionInEditor())
	{
		OutError = TEXT("PIE 未运行，无法添加客户端");
		return FString();
	}

	GEditor->RequestLateJoin();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("已请求添加新客户端"));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}
