with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "r", encoding="utf-8") as f:
    content = f.read()

# Fix: remove common from same-part fallback score
old = """                else:
                    # Same part, different sub-name.
                    # Accept as fallback (shared texture set within same part)
                    common = sum(1 for a, b in zip(sub_name_clean, stem_sub_clean) if a == b)
                    score = 300 + common"""

new = """                else:
                    # Same part, different sub-name.
                    # Accept as fallback (shared texture set within same part)
                    score = 300"""

content = content.replace(old, new)
print("Fixed" if new in content else "Not found")

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "w", encoding="utf-8") as f:
    f.write(content)