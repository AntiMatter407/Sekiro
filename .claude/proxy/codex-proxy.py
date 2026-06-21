#!/usr/bin/env python3
"""
Lightweight OpenAI Chat Completions pass-through gateway for Codex CLI / desktop.

Exposes a pure OpenAI-compatible API on port 7860 (configurable via CODEX_GATEWAY_PORT).
Routes ``/v1/chat/completions`` and ``/v1/models`` upstream using the same key/URL
configuration as proxy.py (DeepSeek / GLM / Qwen).
"""
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from proxy import ROUTES, DEFAULT_ALIAS, resolve_route, strip_claude_prefix, json_dumps, parse_json_bytes, log

PORT = int(os.environ.get("CODEX_GATEWAY_PORT", "7860"))
HOST = os.environ.get("CLAUDE_GATEWAY_HOST", "127.0.0.1")

# ---------------------------------------------------------------------------
# Codex expects bare model ids (no "claude-" prefix) and a pure OpenAI
# Chat Completions response shape.
# ---------------------------------------------------------------------------

class CodexProxyHandler(BaseHTTPRequestHandler):
    server_version = "CodexGateway/1.0"

    def log_message(self, fmt, *args):
        log("[codex] %s %s -> %s" % (self.command, self.path, fmt % args))

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
        self.send_json(status, {"error": {"type": err_type, "message": str(message)}})

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        log("[codex] >>> GET %s" % path)
        if path == "/v1/models":
            self.handle_models()
        elif path in ("/health", "/"):
            self.send_json(200, {"ok": True, "models": len(ROUTES)})
        else:
            self.send_error_json(404, "not found", "not_found_error")

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        log("[codex] >>> POST %s" % path)
        if path == "/v1/chat/completions":
            self.handle_chat_completions()
        else:
            self.send_error_json(404, "not found", "not_found_error")

    # ------------------------------------------------------------------
    # Models
    # ------------------------------------------------------------------
    def handle_models(self):
        models = []
        for alias in ROUTES:
            models.append({
                "id": alias,
                "object": "model",
                "created": int(__import__("time").time()),
                "owned_by": "proxy",
            })
        self.send_json(200, {
            "object": "list",
            "data": models,
        })
        log("[codex]    -> 200 models=%d" % len(models))

    # ------------------------------------------------------------------
    # Chat Completions — transparent pass-through
    # ------------------------------------------------------------------
    def handle_chat_completions(self):
        try:
            data = self.read_json_body()
        except Exception as exc:
            log("[codex]    bad request: %s" % exc)
            self.send_error_json(400, exc)
            return

        # Strip optional provider/ prefix: "deepseek/deepseek-chat" -> "deepseek-chat"
        raw_model = data.get("model", "")
        short = raw_model.split("/", 1)[-1] if "/" in raw_model else raw_model
        alias, upstream_url, api_key, upstream_model = resolve_route(short)
        stream = bool(data.get("stream", False))
        log("[codex]    model=%s upstream_model=%s stream=%s" % (alias, upstream_model, stream))

        data["model"] = upstream_model

        req_headers = {
            "Content-Type": "application/json",
            "Accept": "application/json" if not stream else "text/event-stream",
        }
        if api_key:
            req_headers["Authorization"] = "Bearer %s" % api_key

        req = urllib.request.Request(
            upstream_url,
            data=json_dumps(data).encode("utf-8"),
            headers=req_headers,
            method="POST",
        )

        if stream:
            self._proxy_stream(req)
        else:
            self._proxy_json(req)

    def _proxy_json(self, req):
        try:
            with urllib.request.urlopen(req, timeout=180) as resp:
                upstream = json.loads(resp.read().decode("utf-8"))
            self.send_json(200, upstream)
            log("[codex]    -> 200")
        except urllib.error.HTTPError as exc:
            body = exc.read()
            log("[codex]    upstream HTTP %s: %s" % (exc.code, body[:300].decode("utf-8", "replace")))
            self.send_response(exc.code)
            self.send_header("Content-Type", exc.headers.get("Content-Type", "application/json"))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except Exception as exc:
            log("[codex]    upstream error: %s" % exc)
            self.send_error_json(502, exc, "api_error")

    def _proxy_stream(self, req):
        try:
            with urllib.request.urlopen(req, timeout=180) as resp:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "close")
                self.end_headers()
                for raw_line in resp:
                    self.wfile.write(raw_line)
                    self.wfile.flush()
                self.close_connection = True
                log("[codex]    -> 200 stream")
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", "replace")
            log("[codex]    upstream HTTP %s: %s" % (exc.code, body[:300]))
            self.send_error_json(exc.code, body, "api_error")
        except (BrokenPipeError, ConnectionResetError):
            log("[codex]    client disconnected")
        except Exception as exc:
            log("[codex]    stream error: %s" % exc)
            try:
                self.send_error_json(502, exc, "api_error")
            except Exception:
                pass


def main():
    log("[codex] Starting Codex gateway on http://%s:%d (OpenAI Chat Completions)" % (HOST, PORT))
    log("[codex] Models: %s" % ", ".join(name for name in ROUTES))
    server = ThreadingHTTPServer((HOST, PORT), CodexProxyHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("[codex] Shutting down")
        server.shutdown()


if __name__ == "__main__":
    main()