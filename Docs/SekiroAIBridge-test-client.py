#!/usr/bin/env python3
"""SekiroAIBridge 自动化测试客户端

用法:
    python test_client.py

前提: UE 编辑器已启动，SekiroAIBridge 插件已加载（默认监听 127.0.0.1:9877）
"""

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
            line = await asyncio.wait_for(self.reader.readline(), timeout)
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
        proj = data.get("project", data)
        if isinstance(proj, str):
            proj = json.loads(proj)
        assert "name" in proj
        assert "engineVersion" in proj

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

    async def test_compile_all(self):
        r = await self.request("tools/call", {
            "name": "compile.run",
            "arguments": {"target": "all"}
        })
        content = r["result"]["content"][0]["text"]
        data = json.loads(content)
        assert "totalBlueprints" in data
        assert "compiled" in data

    async def test_asset_info_not_found(self):
        r = await self.request("tools/call", {
            "name": "asset",
            "arguments": {"action": "info", "path": "/Game/DoesNotExist"}
        })
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
        print("错误: 无法连接 -- 确认 UE 编辑器已启动且服务端正在运行")
        sys.exit(1)

    await tester.test("ping", tester.test_ping)
    await tester.test("tools/list 返回6个工具", tester.test_tools_list)
    await tester.test("editor.query 项目信息", tester.test_editor_query_project)
    await tester.test("editor.query 关卡信息", tester.test_editor_query_level)
    await tester.test("缺少 jsonrpc 字段", tester.test_missing_jsonrpc)
    await tester.test("未知方法", tester.test_unknown_method)
    await tester.test("通知无响应", tester.test_notification_no_response)
    await tester.test("JSON 解析错误", tester.test_parse_error)
    await tester.test("未知工具", tester.test_tool_not_found)
    await tester.test("缺失必填参数", tester.test_missing_required_param)
    await tester.test("编译所有 BP", tester.test_compile_all)
    await tester.test("查询不存在的资产", tester.test_asset_info_not_found)

    await tester.disconnect()

    print(f"\n{'=' * 60}")
    print(f"总计 {tester.total} | 通过 {tester.passed} | 失败 {tester.failed}")
    print(f"{'=' * 60}")

    return 0 if tester.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
