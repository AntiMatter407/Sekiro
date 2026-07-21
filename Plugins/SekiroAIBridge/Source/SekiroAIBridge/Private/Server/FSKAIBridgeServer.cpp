#include "Server/FSKAIBridgeServer.h"
#include "SekiroAIBridgeLog.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/TcpSocketBuilder.h"
#include "Async/Async.h"
#include "HAL/RunnableThread.h"

/** 单条消息最大长度 1MB，防止恶意客户端OOM攻击 */
static constexpr int32 MaxLineLength = 1024 * 1024;

// ============================================================================
// 构造/析构
// ============================================================================

FSKAIBridgeServer::FSKAIBridgeServer(int32 InPort)
	: Port(InPort)
	, Thread(nullptr)
	, bRunning(false)
	, bStopping(false)
	, ListenSocket(nullptr)
{
}

FSKAIBridgeServer::~FSKAIBridgeServer()
{
	Shutdown();
}

// ============================================================================
// FRunnable 接口
// ============================================================================

bool FSKAIBridgeServer::Init()
{
	UE_LOG(LogSekiroAIBridge, Log, TEXT("TCP服务端线程初始化中..."));
	return true;
}

uint32 FSKAIBridgeServer::Run()
{
	UE_LOG(LogSekiroAIBridge, Log, TEXT("TCP服务端启动，监听 127.0.0.1:%d"), Port);

	// 使用 FTcpSocketBuilder 创建监听 socket（替代 FTcpListener）
	ListenSocket = FTcpSocketBuilder(TEXT("SekiroAIBridge"))
		.AsReusable()
		.AsNonBlocking()
		.BoundToAddress(FIPv4Address(127, 0, 0, 1))
		.BoundToPort(Port)
		.Listening(1)
		.WithReceiveBufferSize(65536)
		.Build();

	if (!ListenSocket)
	{
		UE_LOG(LogSekiroAIBridge, Error, TEXT("无法创建TCP监听socket"));
		bRunning = false;
		return 1;
	}

	bRunning = true;

	while (!bStopping)
	{
		bool bHasPending = false;
		if (ListenSocket->HasPendingConnection(bHasPending) && bHasPending)
		{
			FString ClientDesc = TEXT("SekiroAIClient");
			FSocket* NewClient = ListenSocket->Accept(ClientDesc);
			if (NewClient)
			{
				UE_LOG(LogSekiroAIBridge, Log, TEXT("新客户端已连接"));

				{
					FScopeLock Lock(&ClientSocketsLock);
					ClientSockets.Add(NewClient);
				}

				// 处理客户端（阻塞直到断开）
				HandleClient(NewClient);

				// 客户端断开，清理
				{
					FScopeLock Lock(&ClientSocketsLock);
					ClientSockets.Remove(NewClient);
				}

				NewClient->Close();
				ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(NewClient);
				UE_LOG(LogSekiroAIBridge, Log, TEXT("客户端已断开"));

				// 通知GameThread客户端断开（用于重置认证状态等）
				FOnClientDisconnected DisconnectCallback = OnClientDisconnected;
				AsyncTask(ENamedThreads::GameThread, [DisconnectCallback]() mutable
				{
					if (DisconnectCallback.IsBound())
					{
						DisconnectCallback.Execute();
					}
				});
			}
		}

		FPlatformProcess::Sleep(0.05f);
	}

	bRunning = false;
	return 0;
}

void FSKAIBridgeServer::Stop()
{
	bStopping = true;
}

// ============================================================================
// 启动/停止
// ============================================================================

bool FSKAIBridgeServer::Start()
{
	if (Thread)
	{
		UE_LOG(LogSekiroAIBridge, Warning, TEXT("服务端已在运行中"));
		return false;
	}

	Thread = FRunnableThread::Create(this, TEXT("SekiroAIBridgeServer"), 0, TPri_Normal);
	if (!Thread)
	{
		UE_LOG(LogSekiroAIBridge, Error, TEXT("无法创建服务端线程"));
		return false;
	}

	return true;
}

void FSKAIBridgeServer::Shutdown()
{
	if (Thread)
	{
		Stop();
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	if (ListenSocket)
	{
		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
	}

	DisconnectAllClients();
}

// ============================================================================
// 客户端处理
// ============================================================================

void FSKAIBridgeServer::HandleClient(FSocket* ClientSocket)
{
	if (!ClientSocket)
	{
		return;
	}

	ClientSocket->SetNonBlocking(true);

	const int32 BufferSize = 65536;
	TArray<uint8> RecvBuffer;
	RecvBuffer.SetNumUninitialized(BufferSize);

	FString PartialLine;

	while (!bStopping)
	{
		// 处理待发送给当前客户端的响应
		TPair<FSocket*, FString> Outgoing;
		while (OutgoingQueue.Peek(Outgoing))
		{
			if (Outgoing.Key == ClientSocket)
			{
				OutgoingQueue.Dequeue(Outgoing);
				SendToClient(ClientSocket, Outgoing.Value);
			}
			else if (Outgoing.Key == nullptr)
			{
				// 广播消息，发给当前客户端
				OutgoingQueue.Dequeue(Outgoing);
				SendToClient(ClientSocket, Outgoing.Value);
			}
			else
			{
				break;  // 是给其他客户端的消息，后续扩展多客户端时处理
			}
		}

		int32 BytesRead = 0;
		if (!ClientSocket->Recv(RecvBuffer.GetData(), BufferSize, BytesRead, ESocketReceiveFlags::None))
		{
			// 连接断开前，清空当前客户端的所有待发送响应
			while (OutgoingQueue.Peek(Outgoing) && (Outgoing.Key == ClientSocket || Outgoing.Key == nullptr))
			{
				OutgoingQueue.Dequeue(Outgoing);
			}
			break;
		}

		if (BytesRead == 0)
		{
			FPlatformProcess::Sleep(0.01f);
			continue;
		}

		// UTF8转TCHAR，BytesRead是字节数仅作截断参考
		FString RawStr = UTF8_TO_TCHAR(reinterpret_cast<const char*>(RecvBuffer.GetData()));
		FString ReceivedData = RawStr.Left(BytesRead);
		PartialLine += ReceivedData;

		// 防御：限制累积行大小，防止恶意客户端OOM
		if (PartialLine.Len() > MaxLineLength)
		{
			UE_LOG(LogSekiroAIBridge, Warning, TEXT("客户端消息超过最大长度限制 %d 字节，断开连接"), MaxLineLength);
			break;
		}

		// 按行分割处理
		int32 NewlineIdx;
		while ((NewlineIdx = PartialLine.Find(TEXT("\n"))) != INDEX_NONE)
		{
			FString Line = PartialLine.Left(NewlineIdx).TrimStartAndEnd();
			PartialLine.RightChopInline(NewlineIdx + 1);

			if (Line.IsEmpty())
			{
				continue;
			}

			FString LineCopy = Line;
			FOnMessageReceived MessageCallback = OnMessageReceived;
			AsyncTask(ENamedThreads::GameThread, [MessageCallback, LineCopy]() mutable
			{
				if (MessageCallback.IsBound())
				{
					MessageCallback.Execute(LineCopy);
				}
			});
		}
	}
}

bool FSKAIBridgeServer::SendToClient(FSocket* ClientSocket, const FString& Data)
{
	if (!ClientSocket)
	{
		return false;
	}

	FTCHARToUTF8 Converter(*Data);
	const char* UTF8Data = Converter.Get();
	int32 BytesToSend = Converter.Length();

	int32 BytesSent = 0;
	return ClientSocket->Send(reinterpret_cast<const uint8*>(UTF8Data), BytesToSend, BytesSent);
}

void FSKAIBridgeServer::DisconnectAllClients()
{
	FScopeLock Lock(&ClientSocketsLock);
	for (FSocket* Socket : ClientSockets)
	{
		if (Socket)
		{
			Socket->Close();
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		}
	}
	ClientSockets.Empty();
}

// ============================================================================
// GameThread调用的接口
// ============================================================================

int32 FSKAIBridgeServer::GetClientCount()
{
	FScopeLock Lock(&ClientSocketsLock);
	return ClientSockets.Num();
}

void FSKAIBridgeServer::EnqueueResponse(const FString& ResponseJson)
{
	if (!bRunning)
	{
		return;
	}

	FScopeLock Lock(&ClientSocketsLock);
	for (FSocket* Socket : ClientSockets)
	{
		OutgoingQueue.Enqueue(TPair<FSocket*, FString>(Socket, ResponseJson));
	}
}

void FSKAIBridgeServer::BroadcastNotification(const FString& NotificationJson)
{
	if (!bRunning)
	{
		return;
	}

	FScopeLock Lock(&ClientSocketsLock);
	// 使用 nullptr socket 标记为广播
	OutgoingQueue.Enqueue(TPair<FSocket*, FString>(nullptr, NotificationJson));
}
