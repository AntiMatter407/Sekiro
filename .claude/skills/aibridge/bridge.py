#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SekiroAIBridge Python 桥接脚本

用法:
    python bridge.py <action> [args...]

示例:
    python bridge.py query project
    python bridge.py console "stat fps"
    python bridge.py asset list /Game/
    python bridge.py python "print(1+1)"
    python bridge.py compile all
    python bridge.py blueprint create ...

前提: UE 编辑器已启动，SekiroAIBridge 监听 127.0.0.1:9877
"""

import asyncio
import json
import sys

HOST = "127.0.0.1"
PORT = 9877
TIMEOUT = 600.0


async def send_request(method, params=None, msg_id="1"):
    """发送 JSON-RPC 请求并返回响应"""
    msg = {"jsonrpc": "2.0", "method": method, "id": msg_id}
    if params is not None:
        msg["params"] = params

    try:
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(HOST, PORT), timeout=5.0
        )
        data = json.dumps(msg, ensure_ascii=False) + "\n"
        writer.write(data.encode())
        await writer.drain()

        line = await asyncio.wait_for(reader.readline(), TIMEOUT)
        writer.close()
        await writer.wait_closed()

        if not line.strip():
            return {"error": "空响应"}
        return json.loads(line.decode().strip())
    except asyncio.TimeoutError:
        return {"error": "连接超时"}
    except ConnectionRefusedError:
        return {"error": "连接被拒绝 — UE 编辑器未启动或 SekiroAIBridge 未加载"}
    except Exception as e:
        return {"error": f"通信错误: {e}"}


def format_response(r):
    """格式化响应为可读输出（支持 JSON-RPC 和直接返回）"""
    if "error" in r:
        return f"[错误] {r['error']}"

    # JSON-RPC 格式: {"result": {"content": [...]}}
    result = r.get("result")
    if result is not None and isinstance(result, dict):
        content = result.get("content", [])
        if content and isinstance(content, list):
            texts = []
            for item in content:
                if isinstance(item, dict) and item.get("type") == "text":
                    try:
                        parsed = json.loads(item["text"])
                        texts.append(json.dumps(parsed, indent=2, ensure_ascii=False))
                    except (json.JSONDecodeError, TypeError):
                        texts.append(item["text"])
            return "\n".join(texts)
        return json.dumps(result, indent=2, ensure_ascii=False)

    # 直接返回格式（如 compile cpp）
    return json.dumps(r, indent=2, ensure_ascii=False)


# ==================== 命令路由 ====================

async def cmd_query(args):
    """editor.query — 查询编辑器状态"""
    action_map = {
        "project": "project", "level": "level",
        "selection": "selection", "time": "time", "all": "all"
    }
    action = action_map.get(args[0], args[0]) if args else "all"
    return await send_request("tools/call", {
        "name": "editor.query",
        "arguments": {"action": action}
    })


async def cmd_console(args):
    """console.execute — 执行控制台命令"""
    if not args:
        return {"error": "用法: console <命令>，例如: console \"stat fps\""}
    return await send_request("tools/call", {
        "name": "console.execute",
        "arguments": {"command": " ".join(args)}
    })


async def cmd_asset(args):
    """asset — 资产操作"""
    if not args:
        return {"error": "用法: asset <list|info|create|delete|rename> [参数...]"}

    action = args[0]
    if action == "list":
        path = args[1] if len(args) > 1 else "/Game/"
        return await send_request("tools/call", {
            "name": "asset",
            "arguments": {"action": "list", "path": path}
        })
    elif action == "info":
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: asset info <资产路径>"}
        return await send_request("tools/call", {
            "name": "asset",
            "arguments": {"action": "info", "path": path}
        })
    elif action == "create":
        if len(args) < 3:
            return {"error": "用法: asset create <资产路径> <资产类型>"}
        return await send_request("tools/call", {
            "name": "asset",
            "arguments": {"action": "create", "path": args[1], "type": args[2]}
        })
    elif action == "delete":
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: asset delete <资产路径>"}
        return await send_request("tools/call", {
            "name": "asset",
            "arguments": {"action": "delete", "path": path}
        })
    elif action == "rename":
        if len(args) < 3:
            return {"error": "用法: asset rename <旧路径> <新路径>"}
        return await send_request("tools/call", {
            "name": "asset",
            "arguments": {"action": "rename", "path": args[1], "newPath": args[2]}
        })
    else:
        return {"error": f"未知资产操作: {action}，支持: list, info, create, delete, rename"}


async def cmd_python(args):
    """python.execute — 执行 Python 脚本"""
    if not args:
        return {"error": "用法: python <代码>"}
    return await send_request("tools/call", {
        "name": "python.execute",
        "arguments": {"script": " ".join(args)}
    })


def _find_ubt():
    """根据引擎目录自动探测 UnrealBuildTool.exe 路径"""
    import os
    editor = _find_ue_editor()
    if editor:
        # UBT 在引擎根目录: Engine/Binaries/Win64/UnrealEditor.exe
        # → Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe
        engine_root = os.path.dirname(os.path.dirname(os.path.dirname(editor)))
        ubt = os.path.join(engine_root, "Binaries", "DotNET", "UnrealBuildTool", "UnrealBuildTool.exe")
        if os.path.exists(ubt):
            return ubt
    return None


async def cmd_compile(args):
    """compile.run — 编译 Blueprint/C++"""
    target = args[0] if args else "all"

    # C++ 编译：直接调用 UBT，捕获错误输出
    if target == "cpp":
        return await compile_cpp()

    if target not in ("all", "changed", "selected"):
        return {"error": f"未知编译目标: {target}，支持: all, changed, selected, cpp"}
    return await send_request("tools/call", {
        "name": "compile.run",
        "arguments": {"target": target}
    })


async def compile_cpp():
    """C++ 编译：编辑器在线→Live Coding，离线→UBT编译后自动启动编辑器"""
    import subprocess
    import re
    import os
    import time

    ubt = _find_ubt()
    project = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "Sekiro.uproject"))
    log_file = os.path.expandvars(r"%LOCALAPPDATA%\UnrealBuildTool\Log.txt")

    editor_online = await _ping_editor()
    mode = "livecoding" if editor_online else "ubt"

    if editor_online:
        result = await _compile_via_livecoding(log_file)
    else:
        if not ubt or not os.path.exists(ubt):
            return {"error": f"UBT未找到: {ubt or '(自动探测失败)'}"}
        try:
            proc = subprocess.run(
                [ubt, "SekiroEditor", "Win64", "Development", f"-Project={project}"],
                capture_output=True, text=True, timeout=300
            )
        except subprocess.TimeoutExpired:
            return {"error": "UBT编译超时（300秒）"}
        except Exception as e:
            return {"error": f"UBT执行失败: {e}"}
        result = _parse_compile_output(proc.stdout + proc.stderr)
        result["exitCode"] = proc.returncode

        # 编译成功后自动启动编辑器
        if result.get("success"):
            start_result = await cmd_editor_start([])
            result["editor"] = start_result.get("status", "unknown")
        elif proc.returncode != 0:
            result["editor"] = "skipped (编译失败)"

    if isinstance(result, dict):
        result["mode"] = mode
    return result


async def _ping_editor():
    """检查编辑器是否在线"""
    try:
        r = await send_request("ping")
        result = r.get("result", {})
        return isinstance(result, dict) and "pong" in str(result.get("message", ""))
    except Exception:
        return False


async def _compile_via_livecoding(log_file):
    """通过编辑器 Live Coding 编译，读取 UBT 日志解析结果"""
    import os
    import time

    # 记录日志文件当前状态
    old_mtime = os.path.getmtime(log_file) if os.path.exists(log_file) else 0
    old_size = os.path.getsize(log_file) if os.path.exists(log_file) else 0

    # 触发 Live Coding
    r = await send_request("tools/call", {
        "name": "compile.run",
        "arguments": {"target": "livecoding"}
    })

    # 检查触发是否成功
    if "error" in r:
        # 尝试用console.execute作备选
        r2 = await send_request("tools/call", {
            "name": "console.execute",
            "arguments": {"command": "LiveCoding.Compile"}
        })
        if "error" in r2:
            return {"error": f"无法触发LiveCoding编译: {r.get('error')} | {r2.get('error')}"}

    # 轮询等待 UBT 日志更新（最多等120秒）
    waited = 0
    while waited < 120:
        await asyncio.sleep(3)
        waited += 3
        if os.path.exists(log_file):
            new_mtime = os.path.getmtime(log_file)
            new_size = os.path.getsize(log_file)
            if new_mtime > old_mtime or new_size != old_size:
                # 再等2秒确保写入完成
                await asyncio.sleep(2)
                break

    if not os.path.exists(log_file):
        return {"error": "UBT日志文件未找到"}

    # 读取日志新增部分
    try:
        with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
            if old_size > 0 and old_size < os.path.getsize(log_file):
                f.seek(old_size)
            content = f.read()
    except Exception as e:
        return {"error": f"读取UBT日志失败: {e}"}

    return _parse_compile_output(content)


def _parse_compile_output(output):
    """解析 UBT/MSVC 编译输出，提取结构化错误"""
    import re
    import os

    error_re = re.compile(
        r'^\s*(.+?)\((\d+)\)\s*:\s*(fatal\s+)?error\s+(\w+)\s*:\s*(.+)$',
        re.MULTILINE
    )
    link_error_re = re.compile(
        r'^\s*(LINK)\s*:\s*(fatal\s+)?error\s+(\w+)\s*:\s*(.+)$',
        re.MULTILINE
    )
    ubt_error_re = re.compile(
        r'^\s*(Error|ERROR|BUILD FAILED|Unable to build.*)$',
        re.MULTILINE
    )
    warning_re = re.compile(
        r'^\s*(.+?)\((\d+)\)\s*:\s*warning\s+(\w+)\s*:\s*(.+)$',
        re.MULTILINE
    )

    errors = []
    for m in error_re.finditer(output):
        errors.append({
            "file": os.path.abspath(m.group(1)),
            "line": int(m.group(2)),
            "code": m.group(4),
            "message": m.group(5).strip(),
            "fatal": bool(m.group(3))
        })
    for m in link_error_re.finditer(output):
        errors.append({
            "file": None,
            "line": 0,
            "code": m.group(3),
            "message": m.group(4).strip(),
            "fatal": True
        })
    for m in ubt_error_re.finditer(output):
        msg = m.group(1).strip()
        # 排除正常信息行（非真正的错误）
        if not any(skip in msg for skip in ("Using ", "Building ", "Determining", "Total time", "Executing", "Creating library")):
            errors.append({
                "file": None,
                "line": 0,
                "code": "UBT",
                "message": msg,
                "fatal": True
            })

    warnings = []
    for m in warning_re.finditer(output):
        warnings.append({
            "file": os.path.abspath(m.group(1)),
            "line": int(m.group(2)),
            "code": m.group(3),
            "message": m.group(4).strip()
        })

    lines = output.strip().split("\n")
    summary = "\n".join(lines[-8:]) if len(lines) > 8 else output.strip()

    return {
        "success": len(errors) == 0,
        "errors": errors,
        "warnings": warnings,
        "errorCount": len(errors),
        "warningCount": len(warnings),
        "summary": summary
    }


async def cmd_blueprint(args):
    """blueprint — Blueprint 操作"""
    if not args:
        return {"error": "用法: blueprint <create|addvar|addfunc|addnode|compile> [参数...]"}

    action = args[0]
    if action == "create":
        if len(args) < 2:
            return {"error": "用法: blueprint create <路径> [父类]"}
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": {
                "action": "create",
                "path": args[1],
                "parentClass": args[2] if len(args) > 2 else "Actor"
            }
        })
    elif action == "addvar":
        if len(args) < 3:
            return {"error": "用法: blueprint addvar <蓝图路径> <变量名> <类型>"}
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": {
                "action": "add_variable",
                "path": args[1],
                "variableName": args[2],
                "variableType": args[3]
            }
        })
    elif action == "addfunc":
        if len(args) < 3:
            return {"error": "用法: blueprint addfunc <蓝图路径> <函数名>"}
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": {
                "action": "add_function",
                "path": args[1],
                "functionName": args[2]
            }
        })
    elif action == "addnode":
        if len(args) < 4:
            return {"error": "用法: blueprint addnode <蓝图路径> <函数名> <节点类型>"}
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": {
                "action": "add_node",
                "path": args[1],
                "functionName": args[2],
                "nodeType": args[3]
            }
        })
    elif action == "compile":
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: blueprint compile <蓝图路径>"}
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": {"action": "compile", "path": path}
        })
    else:
        return {"error": f"未知 Blueprint 操作: {action}，支持: create, addvar, addfunc, addnode, compile"}


def _find_ue_editor():
    """跨机器自动探测 UnrealEditor.exe 路径
    优先级: 当前项目.uproject引擎关联 > 注册表GUID > 常见目录 > Epic Launcher清单
    """
    import os
    import json as _json
    import subprocess
    import re

    # ---- 0. 从当前目录的 .uproject 读取引擎关联 ----
    uproject = _find_uproject()
    if uproject:
        try:
            with open(uproject, "r", encoding="utf-8-sig") as f:
                data = _json.load(f)
            assoc = data.get("EngineAssociation", "")
            if assoc and assoc.startswith("{"):
                # GUID → 查注册表
                try:
                    result = subprocess.run(
                        ["reg", "query", f"HKCU\\Software\\Epic Games\\Unreal Engine\\Builds",
                         "/v", assoc],
                        capture_output=True, text=True, timeout=10
                    )
                    m = re.search(r"REG_SZ\s+(.+)", result.stdout)
                    if m:
                        engine_root = m.group(1).strip()
                        exe = os.path.join(engine_root, "Engine", "Binaries", "Win64", "UnrealEditor.exe")
                        if os.path.exists(exe):
                            return exe
                except Exception:
                    pass
        except Exception:
            pass

    # ---- 1. 扫描常见安装目录（最新版本优先） ----
    search_roots = []
    for env_var in ["PROGRAMFILES", "PROGRAMFILES(X86)"]:
        p = os.environ.get(env_var)
        if p:
            search_roots.append(os.path.join(p, "Epic Games"))
    for drive in ["D:", "F:", "E:", "G:"]:
        for sub in ["Program Files\\Epic Games", "UnrealEngine"]:
            search_roots.append(os.path.join(drive, sub))

    for root in search_roots:
        if not os.path.isdir(root):
            continue
        try:
            entries = sorted(os.listdir(root), reverse=True)
        except OSError:
            continue
        for entry in entries:
            if not entry.startswith("UE_"):
                # 也匹配 UnrealEngine- 前缀的自定义构建
                if not entry.startswith("UnrealEngine-"):
                    continue
            exe = os.path.join(root, entry, "Engine", "Binaries", "Win64", "UnrealEditor.exe")
            if os.path.exists(exe):
                return exe
            # 引擎可能直接在 root 下（如 D:\UnrealEngine\UE_5.2）
            if os.path.isdir(os.path.join(root, entry)):
                try:
                    for subentry in os.listdir(os.path.join(root, entry)):
                        if subentry.startswith("UE_") or subentry.startswith("UnrealEngine-"):
                            exe2 = os.path.join(root, entry, subentry, "Engine", "Binaries", "Win64", "UnrealEditor.exe")
                            if os.path.exists(exe2):
                                return exe2
                except OSError:
                    pass

    # ---- 2. 回退：解析 Epic Launcher 安装清单 ----
    launcher_dat = os.path.expandvars(r"%PROGRAMDATA%\\Epic\\UnrealEngineLauncher\\LauncherInstalled.dat")
    if os.path.exists(launcher_dat):
        try:
            data = _json.load(open(launcher_dat, "r", encoding="utf-8-sig"))
            for install in data.get("InstallationList", []):
                loc = install.get("InstallLocation", "")
                exe = os.path.join(loc, "Engine", "Binaries", "Win64", "UnrealEditor.exe")
                if os.path.exists(exe):
                    return exe
        except Exception:
            pass

    return None


def _find_uproject():
    """在当前目录及父目录中查找 .uproject 文件"""
    import os
    cwd = os.getcwd()
    for _ in range(5):
        for f in os.listdir(cwd) if os.path.isdir(cwd) else []:
            if f.endswith(".uproject"):
                return os.path.join(cwd, f)
        parent = os.path.dirname(cwd)
        if parent == cwd:
            break
        cwd = parent
    return None


async def cmd_editor_start(args):
    """启动 UE 编辑器并等待 TCP 就绪"""
    import subprocess
    import os

    ue_exe = _find_ue_editor()
    if not ue_exe:
        return {"error": "未找到 UnrealEditor.exe，请确认已安装 UE 引擎"}

    project = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "Sekiro.uproject"))

    # 先检查是否已在运行
    if await _ping_editor():
        return {"status": "already_running", "message": "编辑器已在运行中"}

    # 启动编辑器（DETACHED_PROCESS 避免 cmd 窗口残留）
    try:
        subprocess.Popen(
            [ue_exe, project, "-log"],
            creationflags=subprocess.CREATE_NO_WINDOW | subprocess.DETACHED_PROCESS
        )
    except Exception as e:
        return {"error": f"启动编辑器失败: {e}"}

    # 等待 TCP 就绪（最多等90秒）
    for i in range(30):
        await asyncio.sleep(3)
        if await _ping_editor():
            return {"status": "started", "message": f"编辑器已启动（{i * 3}秒）"}

    return {"error": "编辑器启动超时（90秒），请手动检查"}


async def cmd_editor_stop(args):
    """关闭 UE 编辑器并清理子进程和 cmd 窗口"""
    import subprocess

    processes = [
        "UnrealEditor.exe",
        "UE4Editor-Win64-DebugGame.exe",
        "LiveCodingConsole.exe",
        "UnrealTraceServer.exe",
        "UnrealCEFSubProcess.exe",
    ]

    killed = []
    for proc in processes:
        try:
            result = subprocess.run(
                ["taskkill", "/F", "/IM", proc],
                capture_output=True, text=True, timeout=10
            )
            if "SUCCESS" in result.stdout or "成功" in result.stdout:
                killed.append(proc)
            elif result.returncode == 0:
                killed.append(proc)
        except Exception:
            pass

    # 清理可能残留的 cmd 窗口（如果编辑器是从 cmd 启动的）
    await asyncio.sleep(1)

    if not killed:
        return {"status": "not_running", "message": "编辑器未在运行"}

    return {"status": "stopped", "message": f"已关闭: {', '.join(killed)}"}


async def cmd_editor_restart(args):
    """关闭并重新启动 UE 编辑器"""
    stop_result = await cmd_editor_stop([])
    await asyncio.sleep(2)
    start_result = await cmd_editor_start([])
    return {"stop": stop_result, "start": start_result}


async def cmd_crash(args):
    """crash — 崩溃检测和分析"""
    action = args[0] if args else "check"
    if action == "check":
        return await crash_check()
    elif action == "analyze":
        return await crash_analyze()
    elif action == "list":
        return crash_list()
    else:
        return {"error": f"用法: crash <check|analyze|list>，当前: {action}"}


def crash_list():
    """列出最近的崩溃报告目录"""
    import os
    import glob

    project = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    crash_patterns = [
        os.path.join(project, "Saved", "Crashes", "UECC-*"),
        os.path.expandvars(r"%LOCALAPPDATA%\UnrealEngine\5.2\Saved\Crashes\UECC-*"),
    ]

    crashes = []
    for pattern in crash_patterns:
        for d in sorted(glob.glob(pattern), key=os.path.getmtime, reverse=True):
            crashes.append({
                "dir": d,
                "time": os.path.getmtime(d),
                "name": os.path.basename(d)
            })

    # 去重
    seen = set()
    unique = []
    for c in crashes:
        if c["name"] not in seen:
            seen.add(c["name"])
            unique.append(c)

    return {"crashes": unique[:10], "count": len(unique)}


async def crash_check():
    """检测最近一次编辑器运行是否崩溃"""
    import os
    import time

    result = crash_list()
    crashes = result.get("crashes", [])

    if not crashes:
        return {"crashed": False, "message": "未发现崩溃报告"}

    latest = crashes[0]
    age_seconds = time.time() - latest["time"]
    recent = age_seconds < 3600  # 1小时内

    return {
        "crashed": recent,
        "latest": latest,
        "ageSeconds": int(age_seconds),
        "allCrashes": result["count"],
        "message": f"最近崩溃: {latest['name']} ({int(age_seconds/60)}分钟前)" if recent else f"最近崩溃: {latest['name']} ({int(age_seconds/3600)}小时前，可能不相关)"
    }


async def crash_analyze():
    """分析最近一次崩溃：提取错误信息、调用栈、定位源码"""
    import os
    import re
    import xml.etree.ElementTree as ET

    result = crash_list()
    crashes = result.get("crashes", [])
    if not crashes:
        return {"error": "未找到崩溃报告"}

    crash_dir = crashes[0]["dir"]
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

    info = {"dir": crash_dir, "name": crashes[0]["name"]}

    # 1. 解析 CrashContext.runtime-xml
    xml_path = os.path.join(crash_dir, "CrashContext.runtime-xml")
    if os.path.exists(xml_path):
        try:
            tree = ET.parse(xml_path)
            root = tree.getroot()

            info["errorMessage"] = root.findtext("ErrorMessage", "")
            info["crashType"] = root.findtext("CrashType", "")
            info["assertion"] = root.findtext("Assertion", "")

            # 提取调用栈模块名
            modules = {}
            for mod in root.findall("Modules/Module"):
                name = mod.findtext("Name", "")
                base = mod.findtext("BaseOfImage", "")
                if name and base:
                    try:
                        modules[base] = name
                    except Exception:
                        pass
            info["modules"] = modules
        except Exception as e:
            info["xmlError"] = str(e)

    # 2. 从日志文件末尾提取关键信息
    log_files = []
    # 先找崩溃目录中的日志
    for f in os.listdir(crash_dir):
        if f.endswith(".log"):
            log_files.append(os.path.join(crash_dir, f))
    # 再找项目日志
    project_log = os.path.join(project_root, "Saved", "Logs", "Sekiro.log")
    if os.path.exists(project_log):
        log_files.append(project_log)

    log_errors = []
    log_files_found = []
    for log_path in log_files[:2]:  # 最多读2个
        log_files_found.append(os.path.basename(log_path))
        try:
            with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
                # 读取最后 100KB
                f.seek(0, os.SEEK_END)
                size = f.tell()
                read_size = min(size, 102400)
                f.seek(size - read_size)
                tail = f.read()

            # 提取 Fatal/Error/Assert/Ensure 行
            error_lines = []
            for line in tail.split("\n"):
                if any(kw in line for kw in ["Fatal error:", "Assertion failed:", "Ensure condition failed:", "Error:"]):
                    error_lines.append(line.strip()[-300:])  # 截断长行

            if error_lines:
                log_errors.extend(error_lines[-20:])  # 最后20条
        except Exception:
            pass

    info["logFiles"] = log_files_found
    info["logErrors"] = log_errors[-15:] if log_errors else []

    # 3. 从错误信息中提取文件路径和行号
    source_hints = []
    source_re = re.compile(
        r'([\w\\/]+\.(?:cpp|h|hpp))[\(:]\s*(\d+)?',
        re.IGNORECASE
    )
    for line in log_errors:
        for m in source_re.finditer(line):
            path = m.group(1)
            line_num = m.group(2)
            # 只保留项目内的文件
            if any(seg in path.lower() for seg in ["sekiro", "sekiroimport", "sekiroaibridge"]):
                hint = {"raw": path, "line": int(line_num) if line_num else 0}
                if not any(h["raw"] == hint["raw"] for h in source_hints):
                    source_hints.append(hint)

            # 全路径匹配
            full_path = os.path.join(project_root, "Plugins", "SekiroAIBridge", "Source", "*", path)
            full_path2 = os.path.join(project_root, "Plugins", "SekiroImport", "Source", "*", path)

    info["sourceHints"] = source_hints
    info["hint"] = (
        "分析崩溃：先看 errorMessage，再看 logErrors 中的 Fatal/Assert 行，"
        "sourceHints 中如果有文件路径则直接定位，否则从错误描述推断"
    )

    return info


async def cmd_ping(args):
    """ping — 检查服务端存活"""
    return await send_request("ping")


async def cmd_tools(args):
    """tools/list — 列出所有工具"""
    return await send_request("tools/list")


async def cmd_editor(args):
    """editor — 编辑器生命周期管理"""
    action = args[0] if args else "help"
    if action == "start":
        return await cmd_editor_start(args[1:])
    elif action == "stop":
        return await cmd_editor_stop(args[1:])
    elif action == "restart":
        return await cmd_editor_restart(args[1:])
    else:
        return {"error": f"用法: editor <start|stop|restart>，当前: {action}"}


COMMANDS = {
    "query":     cmd_query,
    "console":   cmd_console,
    "asset":     cmd_asset,
    "python":    cmd_python,
    "compile":   cmd_compile,
    "blueprint": cmd_blueprint,
    "crash":     cmd_crash,
    "editor":    cmd_editor,
    "ping":      cmd_ping,
    "tools":     cmd_tools,
}


def print_help():
    print("""SekiroAIBridge — UE5 编辑器 AI 桥接

用法: python bridge.py <命令> [参数...]

命令:
  query <project|level|selection|time|all>  查询编辑器状态
  console <命令>                              执行控制台命令
  asset list [路径]                          列出资产
  asset info <路径>                          资产详情
  asset create <路径> <类型>                  创建资产
  asset delete <路径>                        删除资产
  asset rename <旧路径> <新路径>              重命名资产
  python <代码>                               执行 Python 脚本
  compile <all|changed|selected|cpp>         编译 Blueprint / C++
  blueprint create <路径> [父类]              创建 Blueprint
  blueprint addvar <路径> <变量名> <类型>     添加变量
  blueprint addfunc <路径> <函数名>          添加函数
  blueprint addnode <路径> <函数名> <节点>   添加节点
  blueprint compile <路径>                   编译 Blueprint
  editor start                               启动 UE 编辑器（自动清理 cmd 窗口）
  editor stop                                关闭编辑器及子进程（LiveCoding 等）
  editor restart                             重启编辑器
  crash check                                检测最近一次运行是否崩溃
  crash analyze                              分析崩溃：错误信息、调用栈、源码定位
  crash list                                 列出历史崩溃报告
  ping                                       检查服务端存活
  tools                                      列出所有工具

前提: UE 编辑器已启动，SekiroAIBridge 监听 127.0.0.1:9877
      或使用 editor start 命令让 AI 帮你启动。
""")


async def main():
    if len(sys.argv) < 2:
        print_help()
        sys.exit(1)

    cmd = sys.argv[1].lower()
    rest = sys.argv[2:]

    if cmd in ("-h", "--help", "help"):
        print_help()
        sys.exit(0)

    handler = COMMANDS.get(cmd)
    if not handler:
        print(f"未知命令: {cmd}")
        print(f"支持的命令: {', '.join(COMMANDS.keys())}")
        sys.exit(1)

    result = await handler(rest)
    print(format_response(result))

    # 如果有错误，返回非零退出码
    if isinstance(result, dict) and "error" in result:
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
