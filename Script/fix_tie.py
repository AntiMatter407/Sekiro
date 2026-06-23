with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "r", encoding="utf-8") as f:
    content = f.read()

# Find the score comparison line and add tiebreaker
old_cmp = """            if score > best_score:
                best_score = score
                best_stem = stem"""

new_cmp = """            # Prefer higher score; on tie, prefer shorter stem (more generic)
            if score > best_score or (score == best_score and best_stem and len(stem) < len(best_stem)):
                best_score = score
                best_stem = stem"""

content = content.replace(old_cmp, new_cmp)
print("Added tiebreaker" if new_cmp in content else "Failed")

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "w", encoding="utf-8") as f:
    f.write(content)