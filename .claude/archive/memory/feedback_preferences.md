---
name: preferences
description: 用户协作偏好——中文文档、active文件路径
type: feedback
originSessionId: 9d71e0a4-bf52-4119-b2ed-e434ef408d46
---
所有输出文档默认使用中文。
项目进度仅记录在 `active.md`（路径：`<project>/.claude/active.md`），不使用 memory/ 或其他文件跟踪进度。
active.md 是唯一的进度文件。

**Why:** 用户明确要求，避免进度信息分散在多个文件。
**How to apply:** 每次会话结束后或有进展时更新 active.md，不要在 memory/ 中保存进度信息。其他 memory 文件仅用于偏好、项目背景等持久信息。
