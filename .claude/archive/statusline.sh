#!/usr/bin/env bash
# Claude Code Game Studios — 状态栏
# 从标准输入接收 JSON，输出单行状态信息。
#
# 段落: ctx% | 模型 | 项目阶段

input=$(cat)

# --- 解析 JSON (jq 搭配 grep 降级方案) ---
if command -v jq &>/dev/null; then
  model=$(echo "$input" | jq -r '.model.display_name // "Unknown"')
  used_pct=$(echo "$input" | jq -r '.context_window.used_percentage // empty')
  cwd=$(echo "$input" | jq -r '.workspace.current_dir // .cwd // ""')
else
  model=$(echo "$input" | grep -oE '"display_name"\s*:\s*"[^"]*"' | head -1 | sed 's/.*: *"//;s/"//')
  used_pct=$(echo "$input" | grep -oE '"used_percentage"\s*:\s*[0-9]+' | head -1 | sed 's/.*: *//')
  cwd=$(echo "$input" | grep -oE '"current_dir"\s*:\s*"[^"]*"' | head -1 | sed 's/.*: *"//;s/"//')
  [ -z "$model" ] && model="Unknown"
fi

# 标准化 Windows 路径
cwd=$(echo "$cwd" | sed 's|\\|/|g')
[ -z "$cwd" ] && cwd="."

# --- 上下文使用率 ---
if [ -n "$used_pct" ]; then
  ctx_label="ctx: ${used_pct}%"
else
  ctx_label="ctx: --"
fi

# --- 项目阶段 (从产出物自动检测) ---
stage=""
concept_file="$cwd/Docs/gdd/game-concept.md"
systems_file="$cwd/Docs/gdd/systems-index.md"
tech_prefs="$cwd/.claude/docs/technical-preferences.md"

has_concept=false
has_systems=false
engine_configured=false
src_count=0

[ -f "$concept_file" ] && has_concept=true
[ -f "$systems_file" ] && has_systems=true

# 检查引擎是否已配置 (非占位符)
if [ -f "$tech_prefs" ]; then
  engine_line=$(grep -m1 '^\*\*Engine\*\*:' "$tech_prefs" 2>/dev/null || true)
  if [ -n "$engine_line" ] && ! echo "$engine_line" | grep -q "TO BE CONFIGURED"; then
    engine_configured=true
  fi
fi

# 统计源文件
if [ -d "$cwd/Source" ]; then
  src_count=$(find "$cwd/Source" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.cs" -o -name "*.lua" \) 2>/dev/null | wc -l | tr -d ' ')
fi

# 确定阶段 (从最先进的状态向后检查)
if [ "$src_count" -ge 10 ] 2>/dev/null; then
  stage="Production"
elif [ "$engine_configured" = true ]; then
  stage="Pre-Production"
elif [ "$has_systems" = true ]; then
  stage="Technical Setup"
elif [ "$has_concept" = true ]; then
  stage="Systems Design"
else
  stage="Concept"
fi

# --- 组装 ---
printf "%s" "${ctx_label} | ${model} | ${stage}"
