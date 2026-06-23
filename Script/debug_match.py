import sys
sys.path.insert(0, r"F:\ProjectAI\Sekiro\Script")

from sekiro_asset_manager.model_importer import ModelJsonBuilder
import os

# Simulate matching for a few AM materials
available = []
for root in [r"F:\ProjectAI\Sekiro\Extracted\Textures", r"F:\ProjectAI\Sekiro\Extracted\Textures\Textures"]:
    if os.path.isdir(root):
        for f in os.listdir(root):
            if f.lower().endswith(('.png', '.dds', '.tga')):
                available.append(f)

test_mats = ["AM_M_9000_tops", "AM_M_9000_body", "AM_M_9000_tilingchain", "AM_M_9000_#00#", "AM_M_9000_artificialarm"]
for mat_name in test_mats:
    tx = ModelJsonBuilder._match_textures_by_name(mat_name, "AM_M_9000", available)
    print(f"{mat_name}: {tx}")