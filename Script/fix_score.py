import re

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "r", encoding="utf-8") as f:
    content = f.read()

# Find and replace the part-matching score lines
old_part_score = """                else:
                    # Weak: same part but different sub-name
                    # Only accept if sub-names share significant characters
                    common = sum(1 for a, b in zip(sub_name_clean, stem_sub_clean) if a == b)
                    score = 50 + common"""

new_part_score = """                else:
                    # Same part, different sub-name.
                    # Accept as fallback (shared texture set within same part)
                    common = sum(1 for a, b in zip(sub_name_clean, stem_sub_clean) if a == b)
                    score = 300 + common"""

content = content.replace(old_part_score, new_part_score)

# Also lower MIN_SCORE from 200 to make sure same-part matches pass
old_min = "MIN_SCORE = 200"
new_min = "MIN_SCORE = 200"
# Keep at 200 - 300+common passes easily

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "w", encoding="utf-8") as f:
    f.write(content)

print("Updated same-part fallback score to 300")