#!/usr/bin/env python
"""
Sekiro Asset Manager 命令行入口。

用法：
    python Script/sekiro_asset_manager.py config show
    python Script/sekiro_asset_manager.py model import <asset_name>
    python Script/sekiro_asset_manager.py anim import <asset_name>
    python Script/sekiro_asset_manager.py all import <asset_name>

也可以直接用模块模式运行：
    python -m sekiro_asset_manager config show
"""
from sekiro_asset_manager.cli import main

if __name__ == "__main__":
    main()