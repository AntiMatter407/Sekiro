with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "r", encoding="utf-8") as f:
    content = f.read()

# Add all_available list building right after available_stems
old = """                        if f.lower().endswith((".png", ".dds", ".tga")):
                            available_stems.add(os.path.splitext(f)[0].lower())

        resolved = []"""

new = """                        if f.lower().endswith((".png", ".dds", ".tga")):
                            available_stems.add(os.path.splitext(f)[0].lower())
                            all_available.append(f)

        resolved = []"""

content = content.replace(old, new)

# Add all_available initialization
old2 = """        # Build set of available texture stems
        available_stems = set()"""

new2 = """        # Build set of available texture stems + full list for fallback
        available_stems = set()
        all_available = []"""

content = content.replace(old2, new2)

print("Done" if "all_available" in content else "Failed")

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "w", encoding="utf-8") as f:
    f.write(content)