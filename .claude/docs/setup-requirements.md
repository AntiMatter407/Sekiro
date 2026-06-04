# 环境要求

此模板需要安装一些工具才能实现完整功能。所有钩子在缺少工具时会优雅降级——不会有任何问题，但你会失去验证功能。

## 必需工具

| 工具 | 用途 | 安装方式 |
| ---- | ---- | ---- |
| **Git** | 版本控制，分支管理 | [git-scm.com](https://git-scm.com/) |
| **Claude Code** | AI 代理 CLI | `npm install -g @anthropic-ai/claude-code` |

## 推荐工具

| 工具 | 使用者 | 用途 | 安装方式 |
| ---- | ---- | ---- | ---- |
| **jq** | 钩子（8 个中的 4 个） | 在提交/推送/资产/代理钩子中进行 JSON 解析 | 见下方 |
| **Python 3** | 钩子（8 个中的 2 个） | 数据文件的 JSON 验证 | [python.org](https://www.python.org/) |
| **Bash** | 所有钩子 | Shell 脚本执行 | Git for Windows 自带 |

### 安装 jq

**Windows**（以下任选其一）：
```
winget install jqlang.jq
choco install jq
scoop install jq
```

**macOS**：
```
brew install jq
```

**Linux**：
```
sudo apt install jq     # Debian/Ubuntu
sudo dnf install jq     # Fedora
sudo pacman -S jq       # Arch
```

## 平台说明

### Windows
- Git for Windows 包含 **Git Bash**，提供 `settings.json` 中所有钩子所需的 `bash` 命令
- 确保 Git Bash 在你的 PATH 中（通过 Git 安装程序安装时默认如此）
- 钩子使用 `bash .claude/hooks/[name].sh` —— 这在 Windows 上可以正常工作，因为 Claude Code 通过能找到 `bash.exe` 的 Shell 调用命令

### macOS / Linux
- Bash 原生可用
- 通过你的包管理器安装 `jq` 以获得完整的钩子支持

## 验证你的环境

运行以下命令检查前置条件：

```bash
git --version          # 应显示 git 版本
bash --version         # 应显示 bash 版本
jq --version           # 应显示 jq 版本（可选）
python3 --version      # 应显示 python 版本（可选）
```

## 缺少可选工具的影响

| 缺少的工具 | 影响 |
| ---- | ---- |
| **jq** | 提交验证、推送保护、资产验证和代理审计钩子会静默跳过检查。提交和推送仍然正常工作。 |
| **Python 3** | 提交和资产钩子中的 JSON 数据文件验证会被跳过。无效的 JSON 可以在无警告的情况下被提交。 |
| **两者都缺** | 所有钩子仍然正常执行而不报错（exit 0），但不提供任何验证。你将在没有安全网的情况下运行。 |

## 推荐的 IDE

Claude Code 可与任何编辑器配合使用，但此模板针对以下环境进行了优化：
- **VS Code** 配合 Claude Code 扩展
- **Cursor**（兼容 Claude Code）
- 基于 Terminal 的 Claude Code CLI
