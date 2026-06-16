#!/usr/bin/env python3
"""
Claude Code gateway for OpenAI-compatible model providers.

It exposes the Anthropic Messages endpoints that Claude Code expects:
  GET  /v1/models
  POST /v1/messages
  POST /v1/messages/count_tokens

Upstream calls are translated to OpenAI Chat Completions APIs.
"""
import datetime
import json
import math
import os
import sys
import time
import uuid
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("CLAUDE_GATEWAY_PORT", "4000"))
HOST = os.environ.get("CLAUDE_GATEWAY_HOST", "127.0.0.1")
LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "proxy.log")
UPSTREAM_TIMEOUT = int(os.environ.get("CLAUDE_GATEWAY_TIMEOUT", "180"))

def _endpoint(key_url, key_key):
    return (
        os.environ.get(key_url),
        os.environ.get(key_key),
    )

DS = _endpoint("DS_API_URL", "DS_API_KEY")
GLM = _endpoint("GLM_API_URL", "GLM_API_KEY")
QW = _endpoint("QW_API_URL", "QW_API_KEY")

# Hardcoded fallbacks when env vars are not set — keeps the proxy
# launchable without manual env setup in local dev.
if not DS[0]:
    DS = ("https://api.deepseek.com/v1/chat/completions", "sk-256f9a1bae9e478d86fdfe19ab5f7e7c")
if not GLM[0]:
    GLM = ("https://open.bigmodel.cn/api/paas/v4/chat/completions", "1b3fbd092816477791463e4a9486795c.LMugAePe9kjTdA2X")
if not QW[0]:
    QW = ("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions", "sk-ws-H.REEEIMR.87aX.MEQCIGCZR88s0psu345aftrfPzrbqNPLsLIBPkbjBl6ztiE-AiBG8ohGPX_6fsCiclIQtedet1dVRCSKMbZS393Uv1gGxA")

# Public model aliases shown in Claude Code's /model picker. The gateway strips
# the claude- prefix before route lookup, so aliases below appear as claude-xxx.
ROUTES = {
    # DeepSeek aliases. deepseek-chat is the safest OpenAI-compatible model id.
    "deepseek-v4-pro": (DS, "deepseek-chat"),
    "deepseek-v4-flash": (DS, "deepseek-chat"),
    "deepseek-chat": (DS, "deepseek-chat"),
    "deepseek-reasoner": (DS, "deepseek-reasoner"),

    # Zhipu / BigModel aliases.
    "glm-5.1": (GLM, "glm-5.1"),
    "glm-4-plus": (GLM, "glm-4-plus"),
    "glm-4-flash": (GLM, "glm-4-flash"),

    # Alibaba DashScope OpenAI-compatible aliases.
    "qwen-max": (QW, "qwen-max"),
    "qwen-plus": (QW, "qwen-plus"),
    "qwen-flash": (QW, "qwen-flash"),
    "qwen3-coder-plus": (QW, "qwen3-coder-plus"),
}

DEFAULT_ALIAS = os.environ.get("CLAUDE_GATEWAY_DEFAULT_MODEL", "deepseek-chat")


def log(message):
    ts = datetime.datetime.now().strftime("%H:%M:%S")
    line = "[%s] %s" % (ts, message)
    if getattr(sys.stdout, "isatty", lambda: False)():
        try:
            print(line, flush=True)
        except Exception:
            pass
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass


def strip_claude_prefix(model):
    return model[7:] if isinstance(model, str) and model.startswith("claude-") else model


def resolve_route(model):
    alias = strip_claude_prefix(model or DEFAULT_ALIAS)
    if alias not in ROUTES:
        for known in ROUTES:
            if alias.startswith(known) or known.startswith(alias):
                alias = known
                break
    if alias not in ROUTES:
        alias = DEFAULT_ALIAS if DEFAULT_ALIAS in ROUTES else next(iter(ROUTES))
    route, upstream_model = ROUTES[alias]
    return alias, route[0], route[1], upstream_model


def json_dumps(data):
    return json.dumps(data, ensure_ascii=False, separators=(",", ":"))


def parse_json_bytes(body):
    if not body:
        raise ValueError("empty request body")
    return json.loads(body.decode("utf-8"))


def normalize_text_content(content):
    if content is None:
        return ""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts = []
        for block in content:
            if not isinstance(block, dict):
                parts.append(str(block))
                continue
            btype = block.get("type")
            if btype == "text":
                parts.append(block.get("text", ""))
            elif btype == "image":
                parts.append("[image]")
            elif btype == "tool_result":
                parts.append(normalize_tool_result_content(block.get("content", "")))
            elif btype == "tool_use":
                parts.append(json_dumps(block))
        return "\n".join(p for p in parts if p is not None)
    return str(content)


def normalize_tool_result_content(content):
    if content is None:
        return ""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts = []
        for item in content:
            if isinstance(item, dict) and item.get("type") == "text":
                parts.append(item.get("text", ""))
            else:
                parts.append(json_dumps(item) if isinstance(item, (dict, list)) else str(item))
        return "\n".join(parts)
    if isinstance(content, (dict, list)):
        return json_dumps(content)
    return str(content)


def append_anthropic_message(out, msg):
    role = msg.get("role", "user")
    content = msg.get("content", "")

    if not isinstance(content, list):
        out.append({"role": role, "content": normalize_text_content(content)})
        return

    if role == "assistant":
        text_parts = []
        tool_calls = []
        for block in content:
            if not isinstance(block, dict):
                text_parts.append(str(block))
                continue
            if block.get("type") == "text":
                text_parts.append(block.get("text", ""))
            elif block.get("type") == "tool_use":
                args = block.get("input", {})
                if not isinstance(args, str):
                    args = json_dumps(args)
                tool_calls.append({
                    "id": block.get("id") or "call_%s" % uuid.uuid4().hex[:24],
                    "type": "function",
                    "function": {
                        "name": block.get("name", "unknown_tool"),
                        "arguments": args,
                    },
                })
        oai_msg = {"role": "assistant", "content": "\n".join([p for p in text_parts if p]) or None}
        if tool_calls:
            oai_msg["tool_calls"] = tool_calls
        out.append(oai_msg)
        return

    # Anthropic sends tool_result blocks inside a user message. OpenAI expects
    # a separate role=tool message for each result.
    pending_user_text = []
    for block in content:
        if not isinstance(block, dict):
            pending_user_text.append(str(block))
            continue
        btype = block.get("type")
        if btype == "tool_result":
            if pending_user_text:
                out.append({"role": "user", "content": "\n".join(pending_user_text)})
                pending_user_text = []
            out.append({
                "role": "tool",
                "tool_call_id": block.get("tool_use_id") or block.get("id") or "call_unknown",
                "content": normalize_tool_result_content(block.get("content", "")),
            })
        elif btype == "text":
            pending_user_text.append(block.get("text", ""))
        elif btype == "image":
            pending_user_text.append("[image]")
        else:
            pending_user_text.append(json_dumps(block))
    if pending_user_text or not content:
        out.append({"role": "user", "content": "\n".join(pending_user_text)})


def anthropic_tools_to_openai(tools):
    converted = []
    for tool in tools or []:
        if not isinstance(tool, dict):
            continue
        if tool.get("type") == "function" and "function" in tool:
            converted.append(tool)
            continue
        name = tool.get("name")
        if not name:
            continue
        converted.append({
            "type": "function",
            "function": {
                "name": name,
                "description": tool.get("description", ""),
                "parameters": tool.get("input_schema") or tool.get("parameters") or {"type": "object", "properties": {}},
            },
        })
    return converted


def anthropic_tool_choice_to_openai(choice):
    if not choice:
        return None
    if isinstance(choice, str):
        if choice == "any":
            return "required"
        if choice in ("auto", "none", "required"):
            return choice
        return None
    if isinstance(choice, dict):
        ctype = choice.get("type")
        if ctype == "tool":
            return {"type": "function", "function": {"name": choice.get("name", "")}}
        if ctype in ("auto", "none", "any"):
            return anthropic_tool_choice_to_openai("required" if ctype == "any" else ctype)
        if ctype == "function":
            return choice
    return None


def anthropic_to_openai_payload(data, upstream_model):
    messages = []
    system = data.get("system")
    if system:
        messages.append({"role": "system", "content": normalize_text_content(system)})
    for msg in data.get("messages", []):
        append_anthropic_message(messages, msg)

    payload = {
        "model": upstream_model,
        "messages": messages,
        "max_tokens": int(data.get("max_tokens") or 4096),
        "stream": bool(data.get("stream", False)),
    }
    if "temperature" in data:
        payload["temperature"] = data["temperature"]
    if "top_p" in data:
        payload["top_p"] = data["top_p"]
    if "stop_sequences" in data:
        payload["stop"] = data["stop_sequences"]
    elif "stop" in data:
        payload["stop"] = data["stop"]

    tools = anthropic_tools_to_openai(data.get("tools"))
    if tools:
        payload["tools"] = tools
        choice = anthropic_tool_choice_to_openai(data.get("tool_choice"))
        if choice:
            payload["tool_choice"] = choice
    return payload


def parse_arguments(args):
    if args is None or args == "":
        return {}
    if isinstance(args, dict):
        return args
    try:
        parsed = json.loads(args)
        return parsed if isinstance(parsed, dict) else {"value": parsed}
    except Exception:
        return {"_raw": str(args)}


def stop_reason_from_openai(reason):
    return {
        "stop": "end_turn",
        "length": "max_tokens",
        "tool_calls": "tool_use",
        "content_filter": "stop_sequence",
    }.get(reason or "stop", "end_turn")


def openai_response_to_anthropic(data, model):
    choice = (data.get("choices") or [{}])[0]
    message = choice.get("message") or {}
    content_blocks = []
    text = message.get("content")
    if isinstance(text, list):
        text = normalize_text_content(text)
    if text:
        content_blocks.append({"type": "text", "text": text})

    for call in message.get("tool_calls") or []:
        fn = call.get("function") or {}
        content_blocks.append({
            "type": "tool_use",
            "id": call.get("id") or "call_%s" % uuid.uuid4().hex[:24],
            "name": fn.get("name") or call.get("name") or "unknown_tool",
            "input": parse_arguments(fn.get("arguments", "{}")),
        })

    usage = data.get("usage") or {}
    return {
        "id": data.get("id") or "msg_%s" % uuid.uuid4().hex[:24],
        "type": "message",
        "role": "assistant",
        "content": content_blocks,
        "model": model,
        "stop_reason": stop_reason_from_openai(choice.get("finish_reason")),
        "stop_sequence": None,
        "usage": {
            "input_tokens": int(usage.get("prompt_tokens") or 0),
            "output_tokens": int(usage.get("completion_tokens") or 0),
        },
    }


def estimate_tokens(value):
    if value is None:
        return 0
    if isinstance(value, str):
        return max(1, int(math.ceil(len(value) / 4.0))) if value else 0
    if isinstance(value, (int, float, bool)):
        return 1
    if isinstance(value, list):
        return sum(estimate_tokens(v) for v in value)
    if isinstance(value, dict):
        return sum(estimate_tokens(k) + estimate_tokens(v) for k, v in value.items())
    return estimate_tokens(str(value))


class ClaudeGatewayHandler(BaseHTTPRequestHandler):
    server_version = "ClaudeCodeGateway/1.0"

    def log_message(self, fmt, *args):
        log("%s %s -> %s" % (self.command, self.path, fmt % args))

    def read_json_body(self):
        length_header = self.headers.get("Content-Length")
        if not length_header:
            raise ValueError("missing Content-Length")
        body = self.rfile.read(int(length_header))
        return parse_json_bytes(body)

    def send_json(self, status, data, extra_headers=None):
        raw = json_dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        if extra_headers:
            for k, v in extra_headers.items():
                self.send_header(k, v)
        self.end_headers()
        self.wfile.write(raw)

    def send_error_json(self, status, message, err_type="invalid_request_error"):
        self.send_json(status, {"type": "error", "error": {"type": err_type, "message": str(message)}})

    def send_sse_event(self, event, data):
        raw = ("event: %s\ndata: %s\n\n" % (event, json_dumps(data))).encode("utf-8")
        self.wfile.write(raw)
        self.wfile.flush()

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        log(">>> GET %s" % path)
        if path == "/v1/models":
            self.handle_models()
        elif path == "/health" or path == "/":
            self.send_json(200, {"ok": True, "models": len(ROUTES)})
        else:
            self.send_error_json(404, "not found", "not_found_error")

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        log(">>> POST %s" % path)
        if path == "/v1/messages":
            self.handle_messages()
        elif path == "/v1/messages/count_tokens":
            self.handle_count_tokens()
        else:
            self.send_error_json(404, "not found", "not_found_error")

    def handle_models(self):
        models = []
        for alias in ROUTES:
            models.append({
                "id": "claude-%s" % alias,
                "type": "model",
                "display_name": alias,
                "created_at": "2026-01-01T00:00:00Z",
            })
        self.send_json(200, {
            "data": models,
            "first_id": models[0]["id"] if models else None,
            "last_id": models[-1]["id"] if models else None,
            "has_more": False,
        })
        log("    -> 200 models=%d" % len(models))

    def handle_count_tokens(self):
        try:
            data = self.read_json_body()
            alias, _url, _key, upstream_model = resolve_route(data.get("model"))
            payload = anthropic_to_openai_payload(data, upstream_model)
            count = estimate_tokens(payload.get("messages")) + estimate_tokens(payload.get("tools"))
            self.send_json(200, {"input_tokens": max(1, count)})
            log("    model=%s count_tokens=%d" % (alias, max(1, count)))
        except Exception as exc:
            log("    count_tokens error: %s" % exc)
            self.send_error_json(400, exc)

    def handle_messages(self):
        try:
            data = self.read_json_body()
        except Exception as exc:
            log("    bad request: %s" % exc)
            self.send_error_json(400, exc)
            return

        alias, upstream_url, api_key, upstream_model = resolve_route(data.get("model"))
        stream = bool(data.get("stream", False))
        log("    model=%s upstream_model=%s stream=%s" % (alias, upstream_model, stream))
        payload = anthropic_to_openai_payload(data, upstream_model)

        if stream:
            self.proxy_stream(upstream_url, api_key, payload, "claude-%s" % alias)
        else:
            self.proxy_json(upstream_url, api_key, payload, "claude-%s" % alias)

    def build_upstream_request(self, upstream_url, api_key, payload):
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json" if not payload.get("stream") else "text/event-stream",
        }
        if api_key:
            headers["Authorization"] = "Bearer %s" % api_key
        return urllib.request.Request(
            upstream_url,
            data=json_dumps(payload).encode("utf-8"),
            headers=headers,
            method="POST",
        )

    def proxy_json(self, upstream_url, api_key, payload, model):
        try:
            req = self.build_upstream_request(upstream_url, api_key, payload)
            with urllib.request.urlopen(req, timeout=UPSTREAM_TIMEOUT) as resp:
                upstream = json.loads(resp.read().decode("utf-8"))
            self.send_json(200, openai_response_to_anthropic(upstream, model))
            log("    -> 200")
        except urllib.error.HTTPError as exc:
            body = exc.read()
            log("    upstream HTTP %s: %s" % (exc.code, body[:300].decode("utf-8", "replace")))
            self.send_response(exc.code)
            self.send_header("Content-Type", exc.headers.get("Content-Type", "application/json"))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except Exception as exc:
            log("    upstream error: %s" % exc)
            self.send_error_json(502, exc, "api_error")

    def proxy_stream(self, upstream_url, api_key, payload, model):
        message_id = "msg_%s" % uuid.uuid4().hex[:24]
        content_next_index = 0
        text_open = {"value": False, "index": None}
        tool_states = {}
        finish_reason = "end_turn"

        def start_text():
            nonlocal content_next_index
            if not text_open["value"]:
                text_open["value"] = True
                text_open["index"] = content_next_index
                content_next_index += 1
                self.send_sse_event("content_block_start", {
                    "type": "content_block_start",
                    "index": text_open["index"],
                    "content_block": {"type": "text", "text": ""},
                })

        def stop_text():
            if text_open["value"]:
                self.send_sse_event("content_block_stop", {"type": "content_block_stop", "index": text_open["index"]})
                text_open["value"] = False
                text_open["index"] = None

        def start_tool(oai_index):
            nonlocal content_next_index
            state = tool_states[oai_index]
            if state.get("open"):
                return
            stop_text()
            state["anthropic_index"] = content_next_index
            content_next_index += 1
            state["open"] = True
            self.send_sse_event("content_block_start", {
                "type": "content_block_start",
                "index": state["anthropic_index"],
                "content_block": {
                    "type": "tool_use",
                    "id": state.get("id") or "call_%s" % uuid.uuid4().hex[:24],
                    "name": state.get("name") or "unknown_tool",
                    "input": {},
                },
            })
            pending = state.get("pending_args", "")
            if pending:
                self.send_sse_event("content_block_delta", {
                    "type": "content_block_delta",
                    "index": state["anthropic_index"],
                    "delta": {"type": "input_json_delta", "partial_json": pending},
                })
                state["pending_args"] = ""

        def stop_tools():
            for state in list(tool_states.values()):
                if state.get("open"):
                    self.send_sse_event("content_block_stop", {"type": "content_block_stop", "index": state["anthropic_index"]})
                    state["open"] = False

        try:
            req = self.build_upstream_request(upstream_url, api_key, payload)
            with urllib.request.urlopen(req, timeout=UPSTREAM_TIMEOUT) as resp:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "close")
                self.end_headers()
                self.send_sse_event("message_start", {
                    "type": "message_start",
                    "message": {
                        "id": message_id,
                        "type": "message",
                        "role": "assistant",
                        "content": [],
                        "model": model,
                        "stop_reason": None,
                        "stop_sequence": None,
                        "usage": {"input_tokens": 0, "output_tokens": 0},
                    },
                })

                for raw_line in resp:
                    line = raw_line.decode("utf-8", "replace").strip()
                    if not line or not line.startswith("data:"):
                        continue
                    data_str = line[5:].strip()
                    if data_str == "[DONE]":
                        break
                    try:
                        chunk = json.loads(data_str)
                    except Exception:
                        continue
                    choices = chunk.get("choices") or []
                    if not choices:
                        continue
                    choice = choices[0]
                    delta = choice.get("delta") or {}
                    if choice.get("finish_reason"):
                        finish_reason = stop_reason_from_openai(choice.get("finish_reason"))

                    text = delta.get("content")
                    if text:
                        start_text()
                        self.send_sse_event("content_block_delta", {
                            "type": "content_block_delta",
                            "index": text_open["index"],
                            "delta": {"type": "text_delta", "text": text},
                        })

                    for tc in delta.get("tool_calls") or []:
                        oai_index = int(tc.get("index", 0))
                        state = tool_states.setdefault(oai_index, {"pending_args": "", "open": False})
                        if tc.get("id"):
                            state["id"] = tc.get("id")
                        fn = tc.get("function") or {}
                        if fn.get("name"):
                            state["name"] = fn.get("name")
                        args_part = fn.get("arguments") or ""
                        if not state.get("open") and (state.get("id") or state.get("name")):
                            start_tool(oai_index)
                        if args_part:
                            if state.get("open"):
                                self.send_sse_event("content_block_delta", {
                                    "type": "content_block_delta",
                                    "index": state["anthropic_index"],
                                    "delta": {"type": "input_json_delta", "partial_json": args_part},
                                })
                            else:
                                state["pending_args"] += args_part

                for idx, state in list(tool_states.items()):
                    if not state.get("open"):
                        start_tool(idx)
                stop_text()
                stop_tools()
                self.send_sse_event("message_delta", {
                    "type": "message_delta",
                    "delta": {"stop_reason": finish_reason, "stop_sequence": None},
                    "usage": {"output_tokens": 0},
                })
                self.send_sse_event("message_stop", {"type": "message_stop"})
                self.close_connection = True
                log("    -> 200 stream")
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", "replace")
            log("    upstream HTTP %s: %s" % (exc.code, body[:300]))
            self.send_error_json(exc.code, body, "api_error")
        except (BrokenPipeError, ConnectionResetError):
            log("    client disconnected")
        except Exception as exc:
            log("    stream error: %s" % exc)
            try:
                self.send_error_json(502, exc, "api_error")
            except Exception:
                pass


class ClaudeGatewayServer(ThreadingHTTPServer):
    def handle_error(self, request, client_address):
        import traceback
        log("Unhandled request error from %s: %s" % (client_address, traceback.format_exc()))


def self_test():
    sample = {
        "model": "claude-deepseek-chat",
        "system": "You are concise.",
        "messages": [{"role": "user", "content": "hello"}],
        "tools": [{"name": "list_files", "description": "List files", "input_schema": {"type": "object", "properties": {}}}],
        "max_tokens": 32,
    }
    alias, _url, _key, upstream = resolve_route(sample["model"])
    payload = anthropic_to_openai_payload(sample, upstream)
    assert alias == "deepseek-chat"
    assert payload["model"] == "deepseek-chat"
    assert payload["messages"][0]["role"] == "system"
    assert payload["tools"][0]["type"] == "function"
    fake = {
        "id": "chatcmpl_test",
        "choices": [{"message": {"content": "ok"}, "finish_reason": "stop"}],
        "usage": {"prompt_tokens": 1, "completion_tokens": 1},
    }
    out = openai_response_to_anthropic(fake, "claude-deepseek-chat")
    assert out["content"][0]["text"] == "ok"
    print("self-test ok")


def main():
    if "--self-test" in sys.argv:
        self_test()
        return
    log("Starting Claude Code gateway on http://%s:%d" % (HOST, PORT))
    log("Models: %s" % ", ".join("claude-%s" % name for name in ROUTES))
    server = ClaudeGatewayServer((HOST, PORT), ClaudeGatewayHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("Shutting down")
        server.shutdown()


if __name__ == "__main__":
    main()
