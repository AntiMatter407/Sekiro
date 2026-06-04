# Hook: pre-commit-design-check

## 触发条件

在修改 `Docs/` 或 `assets/data/` 中文件的任何提交（Commit）之前运行。

## 目的

确保设计文档和游戏数据文件在进入版本控制之前保持一致性和完整性。在缺失章节、断开的交叉引用和无效数据传播之前将其捕获。

## 实现

```bash
#!/bin/bash
# Pre-commit hook: Design document and game data validation
# Place in .git/hooks/pre-commit or configure via your hook manager

DESIGN_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '^Docs/')
DATA_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '^assets/data/')

EXIT_CODE=0

# Check design documents for required sections
if [ -n "$DESIGN_FILES" ]; then
    for file in $DESIGN_FILES; do
        if [[ "$file" == *.md ]]; then
            # Check for required sections in GDD documents
            if [[ "$file" == Docs/gdd/* ]]; then
                for section in "Overview" "Detailed" "Edge Cases" "Dependencies" "Acceptance Criteria"; do
                    if ! grep -qi "$section" "$file"; then
                        echo "ERROR: $file missing required section: $section"
                        EXIT_CODE=1
                    fi
                done
            fi
        fi
    done
fi

# Validate JSON data files
if [ -n "$DATA_FILES" ]; then
    for file in $DATA_FILES; do
        if [[ "$file" == *.json ]]; then
            # Find a working Python command
            PYTHON_CMD=""
            for cmd in python python3 py; do
                if command -v "$cmd" >/dev/null 2>&1; then
                    PYTHON_CMD="$cmd"
                    break
                fi
            done
            if [ -n "$PYTHON_CMD" ] && ! "$PYTHON_CMD" -m json.tool "$file" > /dev/null 2>&1; then
                echo "ERROR: $file is not valid JSON"
                EXIT_CODE=1
            fi
        fi
    done
fi

exit $EXIT_CODE
```

## 代理集成

当此 Hook 失败时，提交者应：
1. 对于缺失的设计章节：调用 `game-designer` 代理补全文档
2. 对于无效 JSON：调用 `tools-programmer` 代理或手动修复
