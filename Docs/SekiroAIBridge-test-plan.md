# SekiroAIBridge 模块测试方案

## 测试环境

| 项目 | 说明 |
|------|------|
| 引擎 | UE 5.2 Editor |
| 服务 | 127.0.0.1:9877，编辑器启动后自动运行 |
| 协议 | JSON-RPC 2.0 over TCP，换行分隔 |
| 测试工具 | 下方 `test_client.py` 脚本，或手动 telnet/ncat |
| 数据库 | 无 |

---

## 一、TCP 服务生命周期

### T1.1 – 启动验证
**操作**: 启动 UE 编辑器，打开 Output Log 筛选 `LogSekiroAIBridge`
**预期**: 看到以下日志：
```
AI桥接子系统初始化
TCP服务端启动，监听 127.0.0.1:9877
已注册 6 个工具: editor.query, console.execute, asset, python.execute, compile.run, blueprint
SekiroAIBridge 插件已加载
```

### T1.2 – 端口可连接
**操作**: 在终端执行 `ncat 127.0.0.1 9877`（或 `telnet 127.0.0.1 9877`）
**预期**: 连接成功，Output Log 显示"新客户端已连接"

### T1.3 – 重复启动防护
**操作**: 尝试重复启动服务端（通过代码或配置）
**预期**: 日志显示"服务端已在运行中"，不会创建第二个线程

### T1.4 – 编辑器关闭 → 服务停止
**操作**: 关闭 UE 编辑器
**预期**: Output Log 显示：
```
AI桥接子系统关闭
SekiroAIBridge 插件已卸载
```
无崩溃、无残留 socket

### T1.5 – 端口被占用
**操作**: 先启动另一个进程占用 9877 端口，再启动编辑器
**预期**: 日志显示"无法创建TCP监听socket"，服务端不启动但不崩溃

---

## 二、JSON-RPC 协议

### T2.1 – 有效请求（ping）
**发送**:
```json
{"jsonrpc":"2.0","method":"ping","id":"1"}
```
**预期响应**:
```json
{"jsonrpc":"2.0","id":"1","result":{"message":"pong","running":true,"tools":6}}
```

### T2.2 – 缺少 jsonrpc 字段
**发送**:
```json
{"method":"ping","id":"1"}
```
**预期响应**:
```json
{"jsonrpc":"2.0","error":{"code":-32600,"message":"jsonrpc 字段必须为 \"2.0\""}}
```

### T2.3 – JSON 解析失败
**发送**: `这不是JSON`
**预期响应**:
```json
{"jsonrpc":"2.0","error":{"code":-32700,"message":"JSON解析失败: 这不是JSON"}}
```

### T2.4 – 无效 JSON（缺少 id 的非通知请求）
**发送**:
```json
{"jsonrpc":"2.0","method":"ping"}
```
**预期**: 服务器返回 `InvalidRequest` 错误（id 为 null）

### T2.5 – 未知方法
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/delete","id":"1"}
```
**预期响应**:
```json
{"jsonrpc":"2.0","id":"1","error":{"code":-32601,"message":"未知方法: tools/delete"}}
```

### T2.6 – 通知（无id，不期待响应）
**发送**:
```json
{"jsonrpc":"2.0","method":"ping"}
```
**预期**: 无任何响应（通知模式，沉默处理）

### T2.7 – 空消息
**发送**: 连续两个 `\n`（空行）
**预期**: 不响应、不断开，静默忽略

### T2.8 – 单行超长消息（>1MB）
**操作**: 发送 1.1MB 的超长 JSON 行
**预期**: 连接被断开，日志显示"客户端消息超过最大长度限制 1048576 字节"

---

## 三、tools/list

### T3.1 – 工具列表
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/list","id":"1"}
```
**预期响应**: 返回 6 个工具，每个包含 `name`、`description`、`inputSchema`：
- `editor.query`
- `console.execute`
- `asset`
- `python.execute`
- `compile.run`
- `blueprint`

### T3.2 – Schema 完整性
对上述响应中的每个工具验证：
- `inputSchema.type` = `"object"`
- `inputSchema.properties` 包含对应字段（action/path/command/script/target 等）
- `inputSchema.required` 数组不为空

---

## 四、安全认证

### T4.1 – 无 PSK 配置时跳过认证
**前提**: `PreSharedKey` 为空（默认）
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/list","id":"1"}
```
**预期**: 直接返回工具列表，无需认证

### T4.2 – PSK 认证成功
**前提**: 在 Project Settings > Sekiro AI Bridge 设置 `PreSharedKey = "testkey"`
**发送**:
```json
{"jsonrpc":"2.0","method":"auth","params":{"psk":"testkey"},"id":"1"}
```
**预期**:
```json
{"jsonrpc":"2.0","id":"1","result":{"message":"authenticated"}}
```

### T4.3 – PSK 认证失败
**前提**: 同上，PSK = "testkey"
**发送**:
```json
{"jsonrpc":"2.0","method":"auth","params":{"psk":"wrongkey"},"id":"1"}
```
**预期**:
```json
{"jsonrpc":"2.0","id":"1","error":{"code":-32000,"message":"认证失败：PSK不匹配"}}
```

### T4.4 – 未认证时调用工具被拒绝
**前提**: 同上，PSK 已配置，未发送 auth
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/list","id":"1"}
```
**预期**:
```json
{"jsonrpc":"2.0","id":"1","error":{"code":-32000,"message":"未认证，请先发送 auth 消息"}}
```

### T4.5 – 客户端断开后认证重置
**前提**: 已认证的客户端断开 → 重新连接
**发送**:（新连接，不发送 auth）
```json
{"jsonrpc":"2.0","method":"tools/list","id":"1"}
```
**预期**: 再次被要求认证（返回 T4.4 的错误）

### T4.6 – 只读模式拒绝高风险操作
**前提**: 设置 `bReadOnlyMode = true`
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"console.execute","arguments":{"command":"stat fps"}},"id":"1"}
```
**预期**:
```json
{"jsonrpc":"2.0","id":"1","error":{"code":-32000,"message":"只读模式已启用，拒绝高风险操作"}}
```

### T4.7 – 只读模式下只读工具仍可用
**前提**: `bReadOnlyMode = true`
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"editor.query","arguments":{"action":"project"}},"id":"1"}
```
**预期**: 正常返回项目信息（只读工具无需确认，不受只读模式限制）

---

## 五、Tool: editor.query

### T5.1 – 查询关卡
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"editor.query","arguments":{"action":"level"}},"id":"1"}
```
**预期**: 返回当前打开的关卡名（如 `Untitled` 或实际地图名）

### T5.2 – 查询选中 Actor
**操作**: 在视口中选中若干 Actor
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"editor.query","arguments":{"action":"selection"}},"id":"1"}
```
**预期**: 返回 `{"count":N,"actors":["ActorName1",...]}`

### T5.3 – 查询项目信息
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"editor.query","arguments":{"action":"project"}},"id":"1"}
```
**预期**: 返回 `name`（项目名）、`version`（引擎版本号）、`engineVersion`

### T5.4 – 查询所有（action=all）
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"editor.query","arguments":{"action":"all"}},"id":"1"}
```
**预期**: 返回 level + selection + project + editorTime 四个字段

### T5.5 – 缺参数
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"editor.query","arguments":{}},"id":"1"}
```
**预期**: 默认行为 action="all"，返回全部信息

---

## 六、Tool: console.execute（需确认对话框）

### T6.1 – 执行无害命令
**操作**: 在弹出的确认对话框中点击"是"
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"console.execute","arguments":{"command":"stat fps"}},"id":"1"}
```
**预期**: 返回 `{"command":"stat fps","output":"...","success":true}`，视口显示 FPS 统计

### T6.2 – 执行无输出命令
**操作**: 确认对话框点"是"
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"console.execute","arguments":{"command":"stat none"}},"id":"1"}
```
**预期**: output 为 `"(无输出)"`，success 仍为 true

### T6.3 – 用户拒绝确认
**操作**: 在弹出的确认对话框中点击"否"
**发送**: 同上
**预期**:
```json
{"jsonrpc":"2.0","id":"1","error":{"code":-32000,"message":"用户拒绝了此操作"}}
```

### T6.4 – 会话级允许（"全是"）
**操作**: 第一次弹框点"全是"，后续同工具调用不再弹框
**发送**: 连续两次 `console.execute`
**预期**: 第二次不弹确认框，直接执行

### T6.5 – 缺 command 参数
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"console.execute","arguments":{}},"id":"1"}
```
**预期**: error "缺少 command 参数"

---

## 七、Tool: asset

### T7.1 – 列出目录资产
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"asset","arguments":{"action":"list","path":"/Game","recursive":false}},"id":"1"}
```
**预期**: 返回 `/Game` 根目录下的资产列表，`count` + `assets` 数组

### T7.2 – 查询资产信息
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"asset","arguments":{"action":"info","path":"/Game/SomeAsset"}},"id":"1"}
```
**预期**: 返回该资产的 name/class/path/packageName（如有）；如不存在则返回 error

### T7.3 – 检查资产存在
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"asset","arguments":{"action":"exists","path":"/Game/SomeAsset"}},"id":"1"}
```
**预期**: `{"path":"/Game/SomeAsset","exists":true/false}`

### T7.4 – 创建资产（需确认）
**操作**: 确认对话框点"是"
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"asset","arguments":{"action":"create","path":"/Game/TestAsset","class":"DataAsset"}},"id":"1"}
```
**预期**: Content Browser 中出现 `TestAsset`，返回 `{"path":"/Game/TestAsset","class":"DataAsset","success":true}`

### T7.5 – 删除资产（需确认）
**操作**: 确认对话框点"是"
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"asset","arguments":{"action":"delete","path":"/Game/TestAsset"}},"id":"1"}
```
**预期**: 资产被删除，返回 `{"deleted":true}`

### T7.6 – 复制资产
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"asset","arguments":{"action":"duplicate","path":"/Game/TestAsset","destination":"/Game/TestAsset_Copy"}},"id":"1"}
```
**预期**: 创建副本，返回 `{"source":"...","destination":"...","success":true}`

### T7.7 – 重命名资产
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"asset","arguments":{"action":"rename","path":"/Game/OldName","destination":"/Game/NewName"}},"id":"1"}
```
**预期**: 资产被重命名，返回 `{"source":"...","destination":"...","success":true}`

### T7.8 – 保存资产
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"asset","arguments":{"action":"save","path":"/Game/TestAsset"}},"id":"1"}
```
**预期**: `{"saved":true}`，资产被保存

---

## 八、Tool: python.execute（需确认）

### T8.1 – 执行 Python 代码
**操作**: 确认对话框点"是"
**前提**: PythonScriptPlugin 已启用
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"python.execute","arguments":{"script":"print('hello from AI')"}},"id":"1"}
```
**预期**: Output Log 显示 "hello from AI"，返回 `{"success":true,"mode":"script"}`

### T8.2 – PythonScriptPlugin 未启用
**前提**: 禁用 PythonScriptPlugin
**发送**: 同上
**预期**: error "PythonScriptPlugin不可用"

### T8.3 – 缺 script/file 参数
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"python.execute","arguments":{}},"id":"1"}
```
**预期**: error "需要提供 script 或 file 参数"

### T8.4 – 执行 Python 文件
**操作**: 确认对话框点"是"
**前提**: 项目目录下存在 `/TestPython.py`
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"python.execute","arguments":{"file":"D:/Sekiro/TestPython.py"}},"id":"1"}
```
**预期**: 文件被执行，返回 `{"success":true,"mode":"file","file":"D:/Sekiro/TestPython.py"}`

---

## 九、Tool: compile.run

### T9.1 – 编译所有 Blueprint
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"compile.run","arguments":{"target":"all"}},"id":"1"}
```
**预期**: 返回 `{"target":"all","totalBlueprints":N,"compiled":M,"errors":E}`

### T9.2 – 编译单个 Blueprint
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"compile.run","arguments":{"target":"/Game/Blueprints/BP_Foo"}},"id":"1"}
```
**预期**: 如 BP 存在，返回编译状态（UpToDate/Error/Compiled）；不存在则返回 error

### T9.3 – 触发 LiveCoding
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"compile.run","arguments":{"target":"livecoding"}},"id":"1"}
```
**预期**: 返回 `{"target":"livecoding","triggered":true,"note":"LiveCoding编译已触发，请在编辑器中查看结果"}`

### T9.4 – 保存并编译
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"compile.run","arguments":{"target":"save"}},"id":"1"}
```
**预期**: 触发 SaveAll 后再编译所有 BP

---

## 十、Tool: blueprint（需确认）

### T10.1 – 创建 Blueprint
**操作**: 确认对话框点"是"
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"blueprint","arguments":{"action":"create","path":"/Game/Test/BP_Test","parent_class":"Actor"}},"id":"1"}
```
**预期**: Content Browser 出现 `BP_Test`，返回 `{"path":"/Game/Test/BP_Test","name":"BP_Test","parent_class":"Actor","type":"Blueprint"}`

### T10.2 – 添加变量
**操作**: 确认对话框点"是"
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"blueprint","arguments":{"action":"add_variable","path":"/Game/Test/BP_Test","name":"Health","type":"float"}},"id":"1"}
```
**预期**: `BP_Test` 新增 float 变量 `Health`，返回 `{"blueprint":"...","variable":"Health","type":"float","success":true}`

### T10.3 – 添加函数
**操作**: 确认对话框点"是"
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"blueprint","arguments":{"action":"add_function","path":"/Game/Test/BP_Test","name":"DoSomething"}},"id":"1"}
```
**预期**: `BP_Test` 新增函数 `DoSomething`，返回 `{"blueprint":"...","function":"DoSomething","success":true}`

### T10.4 – 添加组件
**操作**: 确认对话框点"是"
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"blueprint","arguments":{"action":"add_component","path":"/Game/Test/BP_Test","name":"MyMesh","type":"StaticMeshComponent"}},"id":"1"}
```
**预期**: 组件被添加到 BP，返回 `{"blueprint":"...","component":"MyMesh","type":"StaticMeshComponent","success":true}`

### T10.5 – 查询 Blueprint 信息
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"blueprint","arguments":{"action":"get_info","path":"/Game/Test/BP_Test"}},"id":"1"}
```
**预期**: 返回变量列表、函数列表、组件列表、接口列表、编译状态

### T10.6 – 编译单个 BP
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"blueprint","arguments":{"action":"compile","path":"/Game/Test/BP_Test"}},"id":"1"}
```
**预期**: 返回编译状态（UpToDate/Error）

### T10.7 – 不存在的 Blueprint
**发送**:
```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"blueprint","arguments":{"action":"get_info","path":"/Game/NotExist"}},"id":"1"}
```
**预期**: error "Blueprint未找到: /Game/NotExist"

---

## 十一、客户端断开

### T11.1 – 正常断开
**操作**: 在 ncat/telnet 中按 Ctrl+C 断开连接
**预期**: Output Log 显示"客户端已断开"，认证状态重置

### T11.2 – 待发送响应被丢弃
**操作**: 发送一个请求后立即断开（不等响应）
**预期**: 队列中该客户端的响应被清空，不发送到已断开的 socket

---

## 十二、边界与压力

### T12.1 – 快速连续请求
**操作**: 脚本循环发送 100 个 `ping` 请求，每个带不同 id
**预期**: 每个都收到正确响应，无丢包、无乱序、无崩溃

### T12.2 – 多个客户端（当前不支持）
**操作**: 两个 telnet 同时连接
**预期**: 后者排队（前一个断开后自动接上），或都被管理在 ClientSockets 中

### T12.3 – 异常 JSON（嵌套深度）
**发送**: 深层嵌套的 JSON（50层）
**预期**: 正常解析或快速失败，不崩溃

### T12.4 – UTF-8 字符
**发送**: JSON 中包含 emoji / 中文 / 日文
**预期**: 正常处理（UTF8_TO_TCHAR 转换正确）

---

## 十三、自动化测试脚本

使用以下 Python 脚本可一键执行大部分协议和安全测试：

```python
#!/usr/bin/env python3
"""SekiroAIBridge 自动化测试客户端"""
import asyncio
import json
import sys

HOST = "127.0.0.1"
PORT = 9877

class Tester:
    def __init__(self):
        self.reader = None
        self.writer = None
        self.total = 0
        self.passed = 0
        self.failed = 0

    async def connect(self):
        self.reader, self.writer = await asyncio.open_connection(HOST, PORT)

    async def disconnect(self):
        self.writer.close()
        await self.writer.wait_closed()

    async def send(self, msg):
        data = json.dumps(msg) + "\n"
        self.writer.write(data.encode())
        await self.writer.drain()

    async def recv(self, timeout=3.0):
        try:
            line = await asyncio.wait_for(
                self.reader.readline(), timeout
            )
            return json.loads(line.decode().strip()) if line.strip() else None
        except asyncio.TimeoutError:
            return None

    async def request(self, method, params=None, msg_id="1"):
        msg = {"jsonrpc": "2.0", "method": method, "id": msg_id}
        if params is not None:
            msg["params"] = params
        await self.send(msg)
        return await self.recv()

    async def test(self, name, fn):
        self.total += 1
        try:
            await fn()
            self.passed += 1
            print(f"  [PASS] {name}")
        except AssertionError as e:
            self.failed += 1
            print(f"  [FAIL] {name}: {e}")
        except Exception as e:
            self.failed += 1
            print(f"  [FAIL] {name}: {type(e).__name__}: {e}")

    # ==================== 测试用例 ====================

    async def test_ping(self):
        r = await self.request("ping")
        assert r["id"] == "1"
        assert r["result"]["message"] == "pong"
        assert r["result"]["running"] is True
        assert isinstance(r["result"]["tools"], int)

    async def test_tools_list(self):
        r = await self.request("tools/list")
        tools = r["result"]["tools"]
        names = [t["name"] for t in tools]
        assert "editor.query" in names
        assert "console.execute" in names
        assert "asset" in names
        assert "python.execute" in names
        assert "compile.run" in names
        assert "blueprint" in names
        for t in tools:
            assert "inputSchema" in t
            assert "description" in t

    async def test_editor_query_project(self):
        r = await self.request("tools/call", {
            "name": "editor.query",
            "arguments": {"action": "project"}
        })
        content = r["result"]["content"][0]["text"]
        data = json.loads(content)
        assert "name" in data
        assert "engineVersion" in data

    async def test_editor_query_level(self):
        r = await self.request("tools/call", {
            "name": "editor.query",
            "arguments": {"action": "level"}
        })
        assert "content" in r["result"]

    async def test_missing_jsonrpc(self):
        await self.send({"method": "ping", "id": "1"})
        r = await self.recv()
        assert r is not None
        assert "error" in r

    async def test_unknown_method(self):
        r = await self.request("nonexistent.method")
        assert r["error"]["code"] == -32601

    async def test_notification_no_response(self):
        await self.send({"jsonrpc": "2.0", "method": "ping"})
        r = await self.recv(timeout=1.0)
        assert r is None

    async def test_parse_error(self):
        self.writer.write(b"not json at all\n")
        await self.writer.drain()
        r = await self.recv()
        assert r is not None
        assert r.get("error", {}).get("code") == -32700

    async def test_tool_not_found(self):
        r = await self.request("tools/call", {
            "name": "nonexistent.tool",
            "arguments": {}
        })
        assert r["error"]["code"] == -32601

    async def test_missing_required_param(self):
        r = await self.request("tools/call", {
            "name": "console.execute",
            "arguments": {}
        })
        assert r["error"]["code"] == -32000
        assert "缺少" in r["error"]["message"]

    async def test_compile_list(self):
        r = await self.request("tools/call", {
            "name": "compile.run",
            "arguments": {"target": "all"}
        })
        content = r["result"]["content"][0]["text"]
        data = json.loads(content)
        assert "totalBlueprints" in data
        assert "compiled" in data
        assert "errors" in data

    async def test_asset_info_not_found(self):
        r = await self.request("tools/call", {
            "name": "asset",
            "arguments": {"action": "info", "path": "/Game/DoesNotExist"}
        })
        # 资产不存在应返回 error
        assert r.get("error") is not None or r["result"].get("content") is not None


async def main():
    print("=" * 60)
    print("SekiroAIBridge 自动化测试")
    print("=" * 60)

    tester = Tester()
    try:
        await tester.connect()
        print(f"已连接 {HOST}:{PORT}\n")
    except ConnectionRefusedError:
        print("错误: 无法连接 — 确认 UE 编辑器已启动且服务端正在运行")
        sys.exit(1)

    await tester.test("ping 成功", tester.test_ping)
    await tester.test("tools/list 返回6个工具", tester.test_tools_list)
    await tester.test("editor.query 项目信息", tester.test_editor_query_project)
    await tester.test("editor.query 关卡信息", tester.test_editor_query_level)
    await tester.test("缺少 jsonrpc 字段", tester.test_missing_jsonrpc)
    await tester.test("未知方法", tester.test_unknown_method)
    await tester.test("通知无响应", tester.test_notification_no_response)
    await tester.test("JSON 解析错误", tester.test_parse_error)
    await tester.test("未知工具", tester.test_tool_not_found)
    await tester.test("缺失必填参数", tester.test_missing_required_param)
    await tester.test("编译所有 BP", tester.test_compile_list)
    await tester.test("查询不存在的资产", tester.test_asset_info_not_found)

    await tester.disconnect()

    print(f"\n{'=' * 60}")
    print(f"总计 {tester.total} | 通过 {tester.passed} | 失败 {tester.failed}")
    print(f"{'=' * 60}")

    return 0 if tester.failed == 0 else 1


if __name__ == "__main__":
    asyncio.run(main())
```

---

## 执行方式

```bash
# 1. 启动 UE 编辑器（服务端自动运行于 9877 端口）
# 2. 执行测试脚本
python Docs/SekiroAIBridge-test-plan.md  # 保存脚本部分为 test_client.py 后运行
# 或手动测试（协议测试、需人参与确认对话框的测试）
ncat 127.0.0.1 9877
```

---

## 测试状态记录

| 编号 | 用例 | 结果 | 备注 |
|------|------|------|------|
| T1.1 | 启动日志 | ⬜ | |
| T1.2 | 端口可连接 | ⬜ | |
| T1.5 | 端口被占用 | ⬜ | |
| T2.1 | ping 请求 | ⬜ | |
| T2.2 | 缺 jsonrpc | ⬜ | |
| T2.4 | 缺 id | ⬜ | |
| T2.5 | 未知方法 | ⬜ | |
| T3.1 | 工具列表 | ⬜ | |
| T4.1 | 无 PSK 跳过认证 | ⬜ | |
| T4.2 | PSK 认证成功 | ⬜ | 需设 PreSharedKey |
| T4.3 | PSK 认证失败 | ⬜ | 需设 PreSharedKey |
| T4.6 | 只读模式 | ⬜ | 需设 bReadOnlyMode |
| T5.1-5.5 | editor.query | ⬜ | |
| T6.1 | 控制台命令（确认） | ⬜ | 人工点击 |
| T6.3 | 用户拒绝确认 | ⬜ | 人工点击 |
| T7.1-7.8 | asset CRUD | ⬜ | 部分需人工确认 |
| T9.1-9.4 | compile.run | ⬜ | |
| T10.1-10.7| blueprint 操作 | ⬜ | 需人工确认 |
| T11.1 | 客户端断开 | ⬜ | |
| T12.1 | 快速连续请求 | ⬜ | |
