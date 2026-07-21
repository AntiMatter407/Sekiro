#!/usr/bin/env python3
"""检查 Content/Script 下所有 Lua 函数是否具有完整 EmmyLua 文档。"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LUA_ROOT = PROJECT_ROOT / "Content" / "Script"
FUNCTION_PATTERN = re.compile(r"\bfunction(?:\s+[A-Za-z_][A-Za-z0-9_:.]*)?\s*\(")
PARAM_PATTERN = re.compile(r"^---@param\s+(\.\.\.|[A-Za-z_][A-Za-z0-9_]*)\s+\S+\s+(.+)$")
RETURN_PATTERN = re.compile(r"^---@return\s+\S+\s+(.+)$")
CHINESE_PATTERN = re.compile(r"[\u4e00-\u9fff]")


@dataclass(frozen=True)
class FunctionDocIssue:
    path: Path
    line: int
    message: str


def extract_parameters(lines: list[str], line_index: int, match: re.Match[str]) -> list[str]:
    signature = lines[line_index][match.end() :]
    current_index = line_index
    while ")" not in signature and current_index + 1 < len(lines):
        current_index += 1
        signature += " " + lines[current_index].strip()

    parameter_text = signature.split(")", 1)[0]
    return [parameter.strip() for parameter in parameter_text.split(",") if parameter.strip()]


def read_doc_block(lines: list[str], line_index: int) -> list[str]:
    doc_lines: list[str] = []
    current_index = line_index - 1
    while current_index >= 0 and lines[current_index].lstrip().startswith("---"):
        doc_lines.append(lines[current_index].strip())
        current_index -= 1
    doc_lines.reverse()
    return doc_lines


def audit_file(path: Path) -> tuple[int, list[FunctionDocIssue]]:
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    issues: list[FunctionDocIssue] = []
    function_count = 0

    for line_index, line in enumerate(lines):
        if line.lstrip().startswith("--"):
            continue

        match = FUNCTION_PATTERN.search(line)
        if match is None:
            continue

        function_count += 1
        display_line = line_index + 1
        parameters = extract_parameters(lines, line_index, match)
        doc_lines = read_doc_block(lines, line_index)
        summary_lines = [
            doc_line
            for doc_line in doc_lines
            if doc_line.startswith("---") and not doc_line.startswith("---@")
        ]
        if not summary_lines:
            issues.append(FunctionDocIssue(path, display_line, "缺少中文职责说明"))
        elif not any(CHINESE_PATTERN.search(summary_line) for summary_line in summary_lines):
            issues.append(FunctionDocIssue(path, display_line, "职责说明必须包含中文语义"))

        documented_parameters = {
            parameter_match.group(1)
            for doc_line in doc_lines
            if (parameter_match := PARAM_PATTERN.match(doc_line)) is not None
        }
        for parameter in parameters:
            if parameter not in documented_parameters:
                issues.append(
                    FunctionDocIssue(path, display_line, f"缺少参数文档：{parameter}")
                )

        for doc_line in doc_lines:
            parameter_match = PARAM_PATTERN.match(doc_line)
            if parameter_match is not None and not CHINESE_PATTERN.search(parameter_match.group(2)):
                issues.append(
                    FunctionDocIssue(path, display_line, f"参数说明必须包含中文：{parameter_match.group(1)}")
                )

        return_matches = [RETURN_PATTERN.match(doc_line) for doc_line in doc_lines]
        return_matches = [return_match for return_match in return_matches if return_match is not None]
        if not return_matches:
            issues.append(FunctionDocIssue(path, display_line, "缺少返回值文档"))
        elif not all(CHINESE_PATTERN.search(return_match.group(1)) for return_match in return_matches):
            issues.append(FunctionDocIssue(path, display_line, "返回值说明必须包含中文语义"))

    return function_count, issues


def main() -> int:
    total_functions = 0
    all_issues: list[FunctionDocIssue] = []
    for path in sorted(LUA_ROOT.rglob("*.lua")):
        function_count, issues = audit_file(path)
        total_functions += function_count
        all_issues.extend(issues)

    for issue in all_issues:
        relative_path = issue.path.relative_to(PROJECT_ROOT)
        print(f"{relative_path}:{issue.line}: {issue.message}")

    documented_functions = total_functions - len({(issue.path, issue.line) for issue in all_issues})
    print(
        f"Lua 函数文档：{documented_functions}/{total_functions} 完整，"
        f"问题 {len(all_issues)} 项"
    )
    return 1 if all_issues else 0


if __name__ == "__main__":
    sys.exit(main())
