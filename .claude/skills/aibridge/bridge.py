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
        return {"error": "用法: asset <list|info|create|create_physics_asset|delete|rename> [参数...]"}

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
    elif action == "create_physics_asset":
        # bridge.py asset create_physics_asset <PA路径> <骨骼网格路径>
        if len(args) < 3:
            return {"error": "用法: asset create_physics_asset <PhysicsAsset路径> <SkeletalMesh路径>"}
        return await send_request("tools/call", {
            "name": "asset",
            "arguments": {
                "action": "create_physics_asset",
                "path": args[1],
                "skeletal_mesh_path": args[2]
            }
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
    elif action == "import_file":
        # bridge.py asset import_file <资产路径> <源文件路径>
        if len(args) < 3:
            return {"error": "用法: asset import_file <资产路径> <源文件路径>"}
        return await send_request("tools/call", {
            "name": "asset",
            "arguments": {"action": "import_file", "path": args[1], "source_file": args[2]}
        })
    else:
        return {"error": f"未知资产操作: {action}，支持: list, info, create, create_physics_asset, delete, rename, import_file"}


async def cmd_python(args):
    """python.execute — 执行 Python 脚本或文件"""
    if not args:
        return {"error": "用法: python <代码>  或  python --file <文件路径>"}

    if args[0] == "--file":
        if len(args) < 2:
            return {"error": "用法: python --file <文件路径> [--args <参数...>]"}
        payload = {"file": args[1]}
        if "--args" in args:
            idx = args.index("--args")
            if idx + 1 < len(args):
                payload["args"] = " ".join(args[idx + 1:])
        return await send_request("tools/call", {
            "name": "python.execute",
            "arguments": payload
        })

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
                "parent_class": args[2] if len(args) > 2 else "Actor"
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
                "name": args[2],
                "type": args[3]
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
                "name": args[2]
            }
        })
    elif action == "addnode":
        if len(args) < 4:
            return {"error": "用法: blueprint addnode <蓝图路径> <图名> <节点类型>"}
        kwargs = {
            "action": "add_node",
            "path": args[1],
            "graph_name": args[2],
            "node_type": args[3]
        }
        # 可选：PrintString 的 InString 参数
        if len(args) > 4:
            kwargs["in_string"] = args[4]
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": kwargs
        })
    elif action == "set_property":
        # bridge.py blueprint set_property <蓝图路径> <属性名> <值> [CDO|组件名]
        if len(args) < 4:
            return {"error": "用法: blueprint set_property <蓝图路径> <属性名> <值> [CDO|组件名]"}
        setp_args = {
            "action": "set_property",
            "path": args[1],
            "name": args[2],
            "value": args[3]
        }
        if len(args) > 4:
            setp_args["target"] = args[4]
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": setp_args
        })
    elif action == "compile":
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: blueprint compile <蓝图路径>"}
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": {"action": "compile", "path": path}
        })
    elif action == "layout":
        # bridge.py blueprint layout <蓝图路径> [图名]
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: blueprint layout <蓝图路径> [图名]"}
        layout_args = {"action": "layout", "path": path}
        if len(args) > 2:
            layout_args["graph_name"] = args[2]
        return await send_request("tools/call", {
            "name": "blueprint",
            "arguments": layout_args
        })
    else:
        return {"error": f"未知 Blueprint 操作: {action}，支持: create, addvar, addfunc, addnode, set_property, compile, layout"}


async def cmd_enhanced_input(args):
    """enhanced_input — Enhanced Input 资产操作"""
    if not args:
        return {"error": "用法: enhanced_input <create_action|create_context|map_key|unmap_key|info> [参数...]"}

    action = args[0]
    if action == "create_action":
        # bridge.py enhanced_input create_action /Game/Input/IA_Jump axis1d
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: enhanced_input create_action <路径> [value_type]"}
        value_type = args[2] if len(args) > 2 else "bool"
        return await send_request("tools/call", {
            "name": "enhanced_input",
            "arguments": {"action": "create_input_action", "path": path, "value_type": value_type}
        })
    elif action == "create_context":
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: enhanced_input create_context <路径>"}
        return await send_request("tools/call", {
            "name": "enhanced_input",
            "arguments": {"action": "create_mapping_context", "path": path}
        })
    elif action == "map_key":
        # bridge.py enhanced_input map_key <IMC路径> <IA路径> <按键>
        if len(args) < 4:
            return {"error": "用法: enhanced_input map_key <IMC路径> <IA路径> <按键>"}
        return await send_request("tools/call", {
            "name": "enhanced_input",
            "arguments": {
                "action": "map_key",
                "context_path": args[1],
                "action_path": args[2],
                "key": args[3]
            }
        })
    elif action == "unmap_key":
        if len(args) < 4:
            return {"error": "用法: enhanced_input unmap_key <IMC路径> <IA路径> <按键>"}
        return await send_request("tools/call", {
            "name": "enhanced_input",
            "arguments": {
                "action": "unmap_key",
                "context_path": args[1],
                "action_path": args[2],
                "key": args[3]
            }
        })
    elif action == "info":
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: enhanced_input info <路径>"}
        return await send_request("tools/call", {
            "name": "enhanced_input",
            "arguments": {"action": "get_info", "path": path}
        })
    elif action == "configure_triggers":
        # bridge.py enhanced_input configure_triggers <路径> <trigger1> [trigger2...]
        if len(args) < 3:
            return {"error": "用法: enhanced_input configure_triggers <路径> <trigger类型列表...>"}
        return await send_request("tools/call", {
            "name": "enhanced_input",
            "arguments": {
                "action": "configure_triggers",
                "path": args[1],
                "triggers": args[2:]
            }
        })
    else:
        return {"error": f"未知操作: {action}，支持: create_action, create_context, map_key, unmap_key, info, configure_triggers"}


async def cmd_anim_blueprint(args):
    """anim_blueprint — 动画蓝图操作"""
    if not args:
        return {"error": "用法: anim_blueprint <create|add_state|add_transition|delete_transition|add_node|info|compile|layout|set_anim_class> [参数...]"}

    action = args[0]
    if action == "create":
        # bridge.py anim_blueprint create /Game/Anim/ABP_Char /Game/Anim/SK_Char
        if len(args) < 3:
            return {"error": "用法: anim_blueprint create <路径> <骨架路径>"}
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": {"action": "create", "path": args[1], "skeleton_path": args[2]}
        })
    elif action == "add_state":
        # bridge.py anim_blueprint add_state /Game/Anim/ABP_Char Idle
        if len(args) < 3:
            return {"error": "用法: anim_blueprint add_state <ABP路径> <状态名>"}
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": {"action": "add_state", "path": args[1], "state_name": args[2]}
        })
    elif action == "add_transition":
        # bridge.py anim_blueprint add_transition <ABP路径> <源状态> <目标状态> [crossfade] [blend_mode] [--auto-rule] [--condition type:var]
        if len(args) < 4:
            return {"error": "用法: anim_blueprint add_transition <ABP路径> <源状态> <目标状态> [crossfade_duration] [blend_mode] [--auto-rule] [--condition type:var]"}
        kwargs = {
            "action": "add_transition",
            "path": args[1],
            "from_state": args[2],
            "to_state": args[3]
        }
        i = 4
        while i < len(args):
            if args[i] == "--auto-rule":
                kwargs["bAutomaticRuleBasedOnSequencePlayerInState"] = True
                i += 1
            elif args[i] == "--condition":
                if i + 1 < len(args):
                    raw = args[i + 1]
                    # JSON 格式（以 { 开头）直接解析
                    if raw.startswith("{"):
                        import json as _json
                        try:
                            kwargs["condition"] = _json.loads(raw)
                        except _json.JSONDecodeError as e:
                            return {"error": f"--condition JSON 解析失败: {e}"}
                        i += 2
                        continue
                    parts = raw.split(":", 1)
                    cond_type = parts[0]
                    if cond_type == "time_remaining":
                        kwargs["condition"] = {"type": "time_remaining"}
                    elif cond_type in ("bool", "not_bool"):
                        if len(parts) < 2:
                            return {"error": f"--condition {cond_type} 需要 variable，格式: {cond_type}:VarName"}
                        kwargs["condition"] = {"type": cond_type, "variable": parts[1]}
                    elif cond_type == "float_compare":
                        sub = parts[1].split(":") if len(parts) > 1 else []
                        if len(sub) < 3:
                            return {"error": "--condition float_compare 格式: float_compare:VarName:Op:Value"}
                        try:
                            val = float(sub[2])
                        except ValueError:
                            return {"error": f"float_compare value 必须是数字: {sub[2]}"}
                        kwargs["condition"] = {"type": "float_compare", "variable": sub[0], "operator": sub[1], "value": val}
                    else:
                        return {"error": f"不支持的条件类型: {cond_type}，支持: bool, not_bool, float_compare, time_remaining, and(JSON)"}
                    i += 2
                else:
                    return {"error": "--condition 需要参数"}
            elif args[i] == "--bidirectional":
                kwargs["bidirectional"] = True
                i += 1
            else:
                try:
                    kwargs["crossfade_duration"] = float(args[i])
                except ValueError:
                    kwargs["blend_mode"] = args[i]
                i += 1
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": kwargs
        })
    elif action == "delete_transition":
        # bridge.py anim_blueprint delete_transition <ABP路径> <源状态> <目标状态>
        if len(args) < 4:
            return {"error": "用法: anim_blueprint delete_transition <ABP路径> <源状态> <目标状态>"}
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": {"action": "delete_transition", "path": args[1], "from_state": args[2], "to_state": args[3]}
        })
    elif action == "add_node":
        # bridge.py anim_blueprint add_node <ABP路径> <状态名> <sequence_player|blend_space_player> <资产路径> [--loop true|false] [--play-rate 1.0] [--pin-x Angle] [--pin-y Speed]
        if len(args) < 5:
            return {"error": "用法: anim_blueprint add_node <ABP路径> <状态名> <节点类型> <动画资产路径> [--loop true|false] [--pin-x VarName] [--pin-y VarName]"}
        node_args = {
            "action": "add_node",
            "path": args[1],
            "state_name": args[2],
            "node_type": args[3],
            "asset_path": args[4]
        }
        i = 5
        while i < len(args):
            if args[i] == "--loop":
                if i + 1 < len(args):
                    node_args["loop"] = args[i + 1].lower() in ("true", "1", "yes")
                    i += 2
                else:
                    return {"error": "--loop 需要参数 (true/false)"}
            elif args[i] == "--play-rate":
                if i + 1 < len(args):
                    node_args["play_rate"] = float(args[i + 1])
                    i += 2
                else:
                    return {"error": "--play-rate 需要参数"}
            elif args[i] == "--pin-x":
                if i + 1 < len(args):
                    if "pin_connections" not in node_args:
                        node_args["pin_connections"] = {}
                    node_args["pin_connections"]["X"] = args[i + 1]
                    i += 2
                else:
                    return {"error": "--pin-x 需要参数 (变量名)"}
            elif args[i] == "--pin-y":
                if i + 1 < len(args):
                    if "pin_connections" not in node_args:
                        node_args["pin_connections"] = {}
                    node_args["pin_connections"]["Y"] = args[i + 1]
                    i += 2
                else:
                    return {"error": "--pin-y 需要参数 (变量名)"}
            else:
                i += 1
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": node_args
        })
    elif action == "info":
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: anim_blueprint info <ABP路径>"}
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": {"action": "get_info", "path": path}
        })
    elif action == "compile":
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: anim_blueprint compile <ABP路径>"}
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": {"action": "compile", "path": path}
        })
    elif action == "set_anim_class":
        # bridge.py anim_blueprint set_anim_class <ABP路径> --character <角色BP路径> [--mesh Mesh]
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: anim_blueprint set_anim_class <ABP路径> --character <角色BP路径> [--mesh 组件名]"}
        kwargs = {"action": "set_anim_class", "path": path}
        i = 2
        while i < len(args):
            if args[i] == "--character" and i + 1 < len(args):
                kwargs["character_bp_path"] = args[i + 1]
                i += 2
            elif args[i] == "--mesh" and i + 1 < len(args):
                kwargs["mesh_component_name"] = args[i + 1]
                i += 2
            else:
                i += 1
        if "character_bp_path" not in kwargs:
            return {"error": "缺少 --character <角色BP路径>"}
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": kwargs
        })
    elif action == "layout":
        # bridge.py anim_blueprint layout <ABP路径>
        path = args[1] if len(args) > 1 else None
        if not path:
            return {"error": "用法: anim_blueprint layout <ABP路径>"}
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": {"action": "layout", "path": path}
        })
    elif action == "rename_node":
        # bridge.py anim_blueprint rename_node <ABP路径> state_machine <新名称>
        # bridge.py anim_blueprint rename_node <ABP路径> state <旧名称> <新名称>
        if len(args) < 4:
            return {"error": "用法: anim_blueprint rename_node <ABP路径> <target> <...>\n"
                             "  state_machine: rename_node <ABP> state_machine <新名称>\n"
                             "  state:        rename_node <ABP> state <旧名称> <新名称>"}
        kwargs = {
            "action": "rename_node",
            "path": args[1],
            "target": args[2],
        }
        if args[2] == "state_machine":
            kwargs["new_name"] = args[3]
        elif args[2] == "state":
            if len(args) < 5:
                return {"error": "用法: anim_blueprint rename_node <ABP> state <旧名称> <新名称>"}
            kwargs["old_name"] = args[3]
            kwargs["new_name"] = args[4]
        return await send_request("tools/call", {
            "name": "anim_blueprint",
            "arguments": kwargs
        })
    else:
        return {"error": f"未知操作: {action}，支持: create, add_state, add_transition, delete_transition, add_node, info, compile, layout, rename_node, set_anim_class"}


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
    """关闭本项目对应的 UE 编辑器，不影响其他项目的编辑器

    通过进程命令行中的 .uproject 路径来区分是否为本项目。
    """
    import subprocess
    import os
    import re

    project_uproject = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "..", "Sekiro.uproject")
    )
    project_name = os.path.splitext(os.path.basename(project_uproject))[0]  # "Sekiro"

    # 要清理的子进程列表（按项目筛选）
    child_processes = [
        "LiveCodingConsole.exe",
        "UnrealTraceServer.exe",
        "UnrealCEFSubProcess.exe",
    ]

    def find_editor_pids():
        """通过 wmic 查找属于本项目的 UE 编辑器 PID"""
        pids = []
        try:
            result = subprocess.run(
                ["wmic", "process", "where", "name='UnrealEditor.exe'", "get", "ProcessId,CommandLine", "/format:csv"],
                capture_output=True, text=True, timeout=10
            )
            for line in result.stdout.strip().split("\n")[1:]:
                if not line.strip():
                    continue
                parts = line.split(",")
                if len(parts) >= 3:
                    cmdline = parts[1] if len(parts) >= 2 else ""
                    pid_str = parts[2] if len(parts) >= 3 else parts[1]
                    if project_uproject.replace("/", "\\") in cmdline or project_uproject.replace("\\", "/") in cmdline:
                        try:
                            pids.append(int(pid_str.strip()))
                        except ValueError:
                            pass
        except Exception:
            pass
        return pids

    killed = []

    # 只杀本项目对应的编辑器进程
    editor_pids = find_editor_pids()
    if editor_pids:
        for pid in editor_pids:
            try:
                result = subprocess.run(
                    ["taskkill", "/F", "/PID", str(pid)],
                    capture_output=True, text=True, timeout=10
                )
                if result.returncode == 0:
                    killed.append(f"UnrealEditor.exe (PID:{pid})")
            except Exception:
                pass
    else:
        return {"status": "not_running", "message": f"未发现 {project_name} 项目对应的编辑器进程"}

    # 清理编辑器启动的子进程（关编辑器后连带清理）
    await asyncio.sleep(1)
    for proc in child_processes:
        try:
            result = subprocess.run(
                ["taskkill", "/F", "/IM", proc],
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                killed.append(proc)
        except Exception:
            pass

    await asyncio.sleep(1)

    if not killed:
        return {"status": "not_running", "message": f"未发现 {project_name} 项目对应的编辑器进程"}

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


async def cmd_pie(args):
    """pie — PIE 控制（启动/停止/暂停/恢复/状态/加客户端）"""
    action = args[0] if args else "status"
    valid_actions = ("start", "stop", "pause", "resume", "status", "late_join")
    valid_modes = ("selected", "standalone", "mobile", "vulkan", "vr", "simulate")

    if action not in valid_actions:
        return {"error": f"未知 PIE action: {action}，支持: {', '.join(valid_actions)}"}

    arguments = {"action": action}

    # 解析位置参数 mode（仅 start 操作）
    mode_idx = 1
    if action == "start" and len(args) > 1 and args[1] in valid_modes:
        arguments["mode"] = args[1]
        mode_idx = 2

    # 解析可选参数
    i = mode_idx
    while i < len(args):
        arg = args[i]
        if arg.startswith("--"):
            key = arg[2:]
            # --flag 形式（布尔标志）
            if key in ("dedicated", "listen", "server", "viewport"):
                arguments[key] = True
            # --key value 形式
            elif i + 1 < len(args) and not args[i + 1].startswith("--"):
                if key == "clients":
                    arguments["clients"] = int(args[i + 1])
                    i += 1
                elif key == "net_mode":
                    arguments["net_mode"] = args[i + 1]
                    i += 1
                elif key == "mode":
                    if args[i + 1] in valid_modes:
                        arguments["mode"] = args[i + 1]
                        i += 1
        i += 1

    return await send_request("tools/call", {
        "name": "pie.control",
        "arguments": arguments
    })


async def cmd_input_simulate(args):
    """input.simulate — 在 PIE 运行时模拟玩家输入"""
    if not args:
        return {"error": "用法: input_simulate <action> [--x <value>] [--y <value>] [--hold <seconds>] [--delay <seconds>]"}

    action = args[0]
    valid_actions = ("attack", "attack_release", "guard", "guard_release", "dodge", "dodge_release",
                     "jump", "jump_release", "interact", "use_item", "healing_gourd", "grapple",
                     "prosthetic", "lock_on", "crouch", "move", "look", "cycle_item_next",
                     "cycle_item_prev", "pause", "menu")

    if action not in valid_actions:
        return {"error": f"未知 action: {action}，支持: {', '.join(valid_actions)}"}

    arguments = {"action": action}

    # 解析可选参数 --x, --y, --hold, --delay
    i = 1
    while i < len(args):
        arg = args[i]
        if arg == "--x" and i + 1 < len(args):
            arguments["value_x"] = float(args[i + 1])
            i += 2
        elif arg == "--y" and i + 1 < len(args):
            arguments["value_y"] = float(args[i + 1])
            i += 2
        elif arg == "--hold" and i + 1 < len(args):
            arguments["hold_time"] = float(args[i + 1])
            i += 2
        elif arg == "--delay" and i + 1 < len(args):
            arguments["delay"] = float(args[i + 1])
            i += 2
        else:
            i += 1

    return await send_request("tools/call", {
        "name": "input.simulate",
        "arguments": arguments
    })


COMMANDS = {
    "query":           cmd_query,
    "console":         cmd_console,
    "asset":           cmd_asset,
    "python":          cmd_python,
    "compile":         cmd_compile,
    "blueprint":       cmd_blueprint,
    "enhanced_input":  cmd_enhanced_input,
    "anim_blueprint":  cmd_anim_blueprint,
    "crash":           cmd_crash,
    "editor":          cmd_editor,
    "pie":             cmd_pie,
    "input_simulate":  cmd_input_simulate,
    "ping":            cmd_ping,
    "tools":           cmd_tools,
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
  blueprint addnode <路径> <图名> <节点类型>  添加K2节点
  blueprint compile <路径>                   编译 Blueprint
  blueprint layout <路径> [图名]             自动排版 Blueprint 图中节点
  enhanced_input create_action <路径> [类型]  创建 InputAction
  enhanced_input create_context <路径>        创建 InputMappingContext
  enhanced_input map_key <IMC> <IA> <键>     绑定按键映射
  enhanced_input unmap_key <IMC> <IA> <键>   移除按键映射
  enhanced_input info <路径>                  查询 EnhancedInput 资产
  enhanced_input configure_triggers <路径> <T..> 配置 InputAction 触发器
  anim_blueprint create <路径> <骨架>         创建 AnimBlueprint
  anim_blueprint add_state <路径> <状态名>    添加状态
  anim_blueprint add_transition <路径> <A> <B> [crossfade] [blend] [--auto-rule] [--condition type:args] [--bidirectional]
    条件类型: bool:Var | not_bool:Var | float_compare:Var:Op:Val | time_remaining | and (传JSON)
  anim_blueprint delete_transition <路径> <A> <B>  删除转换
  anim_blueprint add_node <路径> <状态> <类型> <资产> [--loop tf] [--pin-x Var] [--pin-y Var]
  anim_blueprint info <路径>                  查询 AnimBP 结构
  anim_blueprint compile <路径>               编译 AnimBlueprint
  anim_blueprint layout <路径>                自动排版状态机节点（状态/Entry/内部节点）
  anim_blueprint rename_node <路径> state_machine <新名>  重命名状态机
  anim_blueprint rename_node <路径> state <旧名> <新名>    重命名状态
  anim_blueprint set_anim_class <路径> --character <BP> 将ABP赋给角色Mesh组件
  editor start                               启动 UE 编辑器（自动清理 cmd 窗口）
  editor stop                                关闭编辑器及子进程（LiveCoding 等）
  editor restart                             重启编辑器
  pie start [mode] [--clients N] [--listen]  启动 PIE（模式: selected/standalone/mobile/vulkan/vr/simulate）
  pie stop                                   停止 PIE
  pie pause                                  暂停 PIE
  pie resume                                 恢复 PIE
  pie status                                 查询 PIE 状态
  pie late_join                              添加客户端（多人已运行时）
  pie late_join                              添加客户端（多人已运行时）
  input_simulate <action> [--x <值>] [--y <值>] [--hold <秒>] [--delay <秒>]  在PIE运行时模拟玩家输入
    动作: attack, guard, dodge, jump, interact, use_item, healing_gourd, grapple,
          prosthetic, lock_on, crouch, move, look, cycle_item_next/prev, pause, menu
    --hold <秒>: 长按后自动释放（适用于attack/guard/dodge/jump）
    --delay <秒>: 延迟执行
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