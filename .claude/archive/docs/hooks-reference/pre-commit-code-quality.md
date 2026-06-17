# Hook: pre-commit-code-quality (预提交代码质量检查)

## 触发条件 (Trigger)

在修改 `src/` 目录下文件并尝试提交前运行。

## 目的 (Purpose)

在代码进入版本控制前强制执行编码标准。捕获风格违规、缺失文档、过度复杂的方法，以及应改为数据驱动 (data-driven) 的硬编码 (hardcoded) 值。

## 实现 (Implementation)

```bash
#!/bin/bash
# Pre-commit hook: 代码质量检查
# 根据你的语言和工具链调整具体检查项

CODE_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '^src/')

EXIT_CODE=0

if [ -n "$CODE_FILES" ]; then
    for file in $CODE_FILES; do
        # 检查 gameplay 代码中是否有硬编码的魔术数字 (magic numbers)
        if [[ "$file" == src/gameplay/* ]]; then
            # 查找可能是平衡值的数字字面量
            # 根据你的语言调整模式
            if grep -nE '(damage|health|speed|rate|chance|cost|duration)[[:space:]]*[:=][[:space:]]*[0-9]+' "$file"; then
                echo "WARNING: $file may contain hardcoded gameplay values. Use data files."
                # 仅警告，不阻塞
            fi
        fi

        # 检查没有责任人的 TODO/FIXME
        if grep -nE '(TODO|FIXME|HACK)[^(]' "$file"; then
            echo "WARNING: $file has TODO/FIXME without owner tag. Use TODO(name) format."
        fi

        # 运行特定语言的 linter（取消注释对应行）
        # GDScript: gdlint "$file" || EXIT_CODE=1
        # C#: dotnet format --check "$file" || EXIT_CODE=1
        # C++: clang-format --dry-run -Werror "$file" || EXIT_CODE=1
    done

    # 对修改过的系统运行单元测试 (unit tests)
    # 取消注释并根据你的测试框架进行调整
    # python -m pytest tests/unit/ -x --quiet || EXIT_CODE=1
fi

exit $EXIT_CODE
```

## 代理集成 (Agent Integration)

当此钩子 (hook) 失败时：
1. 风格违规：使用格式化工具自动修复，或调用 `lead-programmer` (主程序员)
2. 硬编码值：调用 `gameplay-programmer` (玩法程序员) 将值外部化
3. 测试失败：调用 `qa-tester` (QA 测试员) 进行诊断，调用 `gameplay-programmer` 进行修复
