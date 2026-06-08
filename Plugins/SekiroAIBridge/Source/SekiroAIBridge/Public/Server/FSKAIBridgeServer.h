#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"

class FSocket;

/**
 * TCP JSON-RPC 服务端
 *
 * 在独立线程中运行（FRunnable），监听 localhost 端口，接受AI客户端连接。
 * 收到的JSON消息通过AsyncTask派发到GameThread执行业务逻辑，
 * 响应通过线程安全队列返回并发送给客户端。
 *
 * 使用 ISocketSubsystem 原始 socket（非 FTcpListener），
 * 因为 UE5.2 的 FTcpListener 已是独立 FRunnable，无法满足自定义线程控制需求。
 */
class SEKIROAIBRIDGE_API FSKAIBridgeServer : public FRunnable
{
public:
	FSKAIBridgeServer(int32 InPort = 9877);
	virtual ~FSKAIBridgeServer();

	// ---- FRunnable 接口 ----
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

	/** 启动服务端线程 */
	bool Start();

	/** 停止服务端线程并等待退出 */
	void Shutdown();

	/** 是否正在运行 */
	bool IsRunning() const { return bRunning; }

	/** 获取监听端口 */
	int32 GetPort() const { return Port; }

	/** 获取活跃客户端连接数 */
	int32 GetClientCount();

	// ---- GameThread调用的接口 ----

	/** 将响应消息加入发送队列（GameThread安全） */
	void EnqueueResponse(const FString& ResponseJson);

	/** 将通知消息广播给所有客户端 */
	void BroadcastNotification(const FString& NotificationJson);

	/** 收到消息时的回调（在GameThread上调用） */
	DECLARE_DELEGATE_OneParam(FOnMessageReceived, const FString& /*JsonLine*/);
	FOnMessageReceived OnMessageReceived;

	/** 客户端断开连接时的回调（在GameThread上调用） */
	DECLARE_DELEGATE(FOnClientDisconnected);
	FOnClientDisconnected OnClientDisconnected;

private:
	/** 处理单个客户端连接（在Run线程中） */
	void HandleClient(FSocket* ClientSocket);

	/** 向单个客户端发送字符串 */
	bool SendToClient(FSocket* ClientSocket, const FString& Data);

	/** 断开所有活跃客户端 */
	void DisconnectAllClients();

	int32 Port;
	FRunnableThread* Thread;
	FThreadSafeBool bRunning;
	FThreadSafeBool bStopping;

	/** 监听socket（Run线程中创建和使用） */
	FSocket* ListenSocket;

	/** 活跃客户端socket列表 */
	TArray<FSocket*> ClientSockets;
	FCriticalSection ClientSocketsLock;

	/** 待发送的响应队列（多生产者-单消费者，Run线程消费） */
	TQueue<TPair<FSocket*, FString>, EQueueMode::Mpsc> OutgoingQueue;
};
