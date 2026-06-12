#!/bin/bash
# Sekiro ABP 生成编排脚本
# 调用 AIToolRunner Commandlet（通用C++接口），逐步创建 BlendSpace + ABP + 关联

set -e

EDITOR="D:/Program Files/Epic Games/UE_5.2/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJECT="D:/Sekiro/Sekiro.uproject"
OUTPUT_PATH="/Game/Characters/Sekiro"
SKELETON="$OUTPUT_PATH/Sekiro_Skeleton"
ANIMS="$OUTPUT_PATH/Animations"

echo "============================================"
echo "Sekiro ABP 生成"
echo "============================================"

# 辅助函数：运行 AIToolRunner
run_tool() {
    local action="$1"
    local args="$2"
    echo ""
    echo "--- $action ---"
    MSYS_NO_PATHCONV=1 "$EDITOR" "$PROJECT" -run=SKAIToolRunner \
        -ToolName=anim_blueprint \
        -ToolArgs="$args" \
        -stdout -unattended 2>&1 | grep "RESULT:" | sed 's/.*RESULT: //'
}

# ================================================================
# Step 1: 创建 BlendSpace
# ================================================================
echo ""
echo "[1/5] 创建 BS_Sekiro_Locomotion..."

BLENDSPACE_ARGS='{"action":"create_blend_space","path":"/Game/Characters/Sekiro/BS_Sekiro_Locomotion","skeleton_path":"/Game/Characters/Sekiro/Sekiro_Skeleton","axes":[{"name":"Direction","min":-180,"max":180,"grid":7},{"name":"Speed","min":0,"max":600,"grid":4}],"samples":[{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Idle_Default","x":0,"y":0},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Walk_Bwd","x":-180,"y":150},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Walk_L","x":-90,"y":150},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Walk_Fwd_L","x":-45,"y":150},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Walk_Fwd","x":0,"y":150},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Walk_Fwd_R","x":45,"y":150},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Walk_R","x":90,"y":150},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Walk_Bwd","x":180,"y":150},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Run_Fast_Fwd_L","x":-45,"y":300},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Run_Fast_Fwd","x":0,"y":300},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Run_Fast_Fwd_R","x":45,"y":300},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Sprint_Fwd_L","x":-45,"y":500},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Sprint_Fwd","x":0,"y":500},{"anim_path":"/Game/Characters/Sekiro/Animations/Anim_Sekiro_Sprint_Fwd_R","x":45,"y":500}]}'

run_tool "create_blend_space" "$BLENDSPACE_ARGS"

# ================================================================
# Step 2: 创建 AnimBlueprint
# ================================================================
echo ""
echo "[2/5] 创建 ABP_Sekiro..."

ABP_ARGS='{"action":"create","path":"/Game/Characters/Sekiro/ABP_Sekiro","skeleton_path":"/Game/Characters/Sekiro/Sekiro_Skeleton","parent_class":"/Script/Sekiro.SekiroAnimInstance"}'

run_tool "create_abp" "$ABP_ARGS"

# ================================================================
# Step 3: 设置 AnimGraph（BlendSpacePlayer → Root）
# ================================================================
echo ""
echo "[3/5] 设置 AnimGraph..."

SETUP_ARGS='{"action":"setup_anim_graph","path":"/Game/Characters/Sekiro/ABP_Sekiro","blend_space_path":"/Game/Characters/Sekiro/BS_Sekiro_Locomotion"}'

run_tool "setup_anim_graph" "$SETUP_ARGS"

# ================================================================
# Step 4: 分配 ABP 给角色
# ================================================================
echo ""
echo "[4/5] 将 ABP_Sekiro 分配给 BP_SekiroCharacter..."

SET_CLASS_ARGS='{"action":"set_anim_class","path":"/Game/Characters/Sekiro/ABP_Sekiro","character_bp_path":"/Game/Gameplay/BP_SekiroCharacter","mesh_component_name":"Mesh"}'

run_tool "set_anim_class" "$SET_CLASS_ARGS"

# ================================================================
# Step 5: 编译 ABP
# ================================================================
echo ""
echo "[5/5] 编译 ABP_Sekiro..."

COMPILE_ARGS='{"action":"compile","path":"/Game/Characters/Sekiro/ABP_Sekiro"}'

run_tool "compile" "$COMPILE_ARGS"

echo ""
echo "============================================"
echo "完成！"
echo "  BS_Sekiro_Locomotion → 创建完成（14 samples）"
echo "  ABP_Sekiro → 创建完成，已分配给 BP_SekiroCharacter.Mesh"
echo "============================================"
