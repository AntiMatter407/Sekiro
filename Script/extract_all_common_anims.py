"""
Streaming extractor for Sekiro_animations.json (1014MB).
Uses byte-level bracket matching to extract animation objects without full JSON load.

Usage:
  python extract_all_common_anims.py [--dry-run] [--output PATH]
"""

import json
import os
import re
import sys
import argparse
import time
from collections import OrderedDict

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Extracted", "Sekiro_animations.json")
DST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Extracted", "Sekiro_common_anims.json")

# ============================================================================
# Animation ID → Readable Name (abbreviated for script space)
# Full map is in the script body
# ============================================================================
ANIMATION_MAP = OrderedDict({
    "000000":"Idle_Default", "000010":"Idle_Combat", "000011":"Idle_Combat_L",
    "000012":"Idle_Combat_R", "000013":"Idle_Combat_Shift",
    "000100":"Walk_Fwd", "000101":"Walk_Fwd_L", "000102":"Walk_Fwd_R",
    "000103":"Walk_Fwd_Stop", "000110":"Walk_Bwd", "000111":"Walk_Bwd_L",
    "000112":"Walk_Bwd_R", "000113":"Walk_Bwd_Stop", "000120":"Walk_L",
    "000121":"Walk_L_Stop", "000122":"Walk_R", "000123":"Walk_R_Stop",
    "000132":"Walk_LSlow", "000133":"Walk_RSlow",
    "000200":"Jog_Fwd", "000201":"Jog_Fwd_L", "000202":"Jog_Fwd_R", "000203":"Jog_Fwd_Stop",
    "000300":"Sprint_Fwd", "000301":"Sprint_Fwd_L", "000302":"Sprint_Fwd_R",
    "000303":"Sprint_Fwd_Stop",
    "000400":"Run_Fast_Fwd", "000401":"Run_Fast_Fwd_L", "000402":"Run_Fast_Fwd_R",
    "000403":"Run_Fast_Fwd_Stop", "000420":"Run_Fast_L", "000421":"Run_Fast_L_Stop",
    "000422":"Run_Fast_R", "000423":"Run_Fast_R_Stop",
    "000500":"Sprint_To_Idle", "000600":"Sprint_To_Idle_Fast",
    "001151":"Idle_WeaponOut", "001152":"Idle_WeaponOut_L",
    "001153":"Idle_WeaponOut_R", "001154":"Idle_WeaponOut_Shift",
    "001200":"Idle_WeaponSheathe",
    "005000":"Turn_L45", "005010":"Turn_L90", "005011":"Turn_L90_Fast",
    "005100":"Turn_R45", "005110":"Turn_R90", "005111":"Turn_R90_Fast",
    "005200":"Turn_L135", "005300":"Turn_R135",
    "005400":"Turn_L180", "005410":"Turn_L180_Fast",
    "005500":"Turn_R180", "005510":"Turn_R180_Fast",
    "020000":"Walk_To_Idle", "020010":"Walk_To_Jog", "021000":"Walk_Stop_Turn",
    "023000":"Run_To_Idle", "023200":"Run_To_Walk", "023300":"Run_To_Jog",
    "024200":"Sprint_To_Run", "024300":"Sprint_To_Jog",
    "030000":"StepDodge_Fwd", "030100":"StepDodge_Bwd",
    "030110":"StepDodge_L", "030200":"StepDodge_R", "030300":"StepDodge_Dash",
    "031400":"Quickstep_Fwd", "031410":"Quickstep_L",
    "031420":"Quickstep_R", "031430":"Quickstep_Bwd",
    "040000":"Jump_Neutral", "040012":"Jump_Fwd", "040100":"Jump_Bwd",
    "040110":"Jump_L", "040120":"Jump_R",
    "040200":"Jump_Land", "040210":"Jump_Fwd_Land",
    "040300":"Jump_Fall", "040310":"Jump_Fall_Long", "040320":"Jump_Ledge_Grab",
    "041400":"Jump_WallKick_Fwd", "041410":"Jump_WallKick_L",
    "041420":"Jump_WallKick_R", "041430":"Jump_WallKick_Bwd",
    "100000":"Attack_R1_Combo01", "100001":"Attack_R1_Combo02",
    "100002":"Attack_R1_Combo03", "100003":"Attack_R1_Combo04",
    "100100":"Attack_R1_Step01", "100101":"Attack_R1_Step02", "100102":"Attack_R1_Step03",
    "100200":"Attack_R1_Dash01", "100201":"Attack_R1_Dash02",
    "100300":"Attack_R1_L_Combo01", "100301":"Attack_R1_L_Combo02",
    "100310":"Attack_R1_L_Step01", "100311":"Attack_R1_L_Step02",
    "100320":"Attack_R1_L_Dash01",
    "100400":"Attack_Charged", "100401":"Attack_Charged_Step",
    "100410":"Attack_Charged_L", "100420":"Attack_Charged_Dash",
    "100500":"Attack_Thrust", "100600":"Attack_Thrust_Charged",
    "100610":"Attack_Thrust_Charged_L", "100800":"Attack_GuardBreak",
    "101100":"Attack_Sprint_R1", "101300":"Attack_Sprint_Thrust",
    "102000":"Attack_Dodge_Fwd", "102100":"Attack_Dodge_L",
    "102110":"Attack_Dodge_R", "102300":"Attack_Dodge_Dash",
    "102310":"Attack_Dodge_Back", "102390":"Attack_Dodge_Charged",
    "102500":"Attack_Slide_Fwd", "102510":"Attack_Slide_L", "102520":"Attack_Slide_R",
    "102900":"Attack_Quickstep_Fwd", "102910":"Attack_Quickstep_L",
    "102930":"Attack_Quickstep_R", "102940":"Attack_Quickstep_Bwd",
    "103000":"Attack_Jump", "103100":"Attack_Jump_Fwd", "103300":"Attack_Jump_Charged",
    "104100":"Attack_Crouch", "104300":"Attack_Crouch_Charged",
    "110000":"CombatArt_Whirlwind", "110001":"CombatArt_Whirlwind_L",
    "110010":"CombatArt_Whirlwind_Alt", "110030":"CombatArt_Nightjar",
    "110031":"CombatArt_Nightjar_Reversal",
    "111000":"CombatArt_Ichimonji", "111001":"CombatArt_Ichimonji_Double",
    "111010":"CombatArt_Ichimonji_Alt", "111011":"CombatArt_Ichimonji_Double_Alt",
    "111030":"CombatArt_Ichimonji_Jump", "111031":"CombatArt_Ichimonji_Double_Jump",
    "112020":"CombatArt_PrayingStrikes", "112021":"CombatArt_PrayingStrikes_Alt",
    "113000":"CombatArt_AshinaCross", "113010":"CombatArt_AshinaCross_Alt",
    "113030":"CombatArt_AshinaCross_Dash",
    "114000":"CombatArt_Shadowrush", "114010":"CombatArt_Shadowrush_Alt",
    "114030":"CombatArt_Shadowrush_Dash",
    "190000":"CombatArt_MortalDraw", "190001":"CombatArt_MortalDraw_Empowered",
    "190010":"CombatArt_MortalDraw_Jump", "190011":"CombatArt_MortalDraw_Jump_Empowered",
    "190030":"CombatArt_OneMind",
    "191000":"CombatArt_SakuraDance", "191200":"CombatArt_Lightning",
    "191400":"CombatArt_HighMonk", "191500":"CombatArt_HighMonk_Leap",
    "192400":"CombatArt_SenThrow", "192500":"CombatArt_PhantomKunai",
    "200000":"Guard_Idle", "200100":"Guard_Raise", "200120":"Guard_Lower",
    "200121":"Guard_Lower_Fast",
    "201000":"Parry_Idle", "201001":"Parry_Deflect",
    "201010":"Parry_Raise", "201011":"Parry_Lower", "201030":"Parry_Deflect_Success",
    "201040":"Parry_Deflect_L", "201045":"Parry_Deflect_R",
    "201050":"Parry_Deflect_Fwd", "201055":"Parry_Deflect_Bwd",
    "201110":"Deflect_Counter_R1",
    "201140":"Deflect_Counter_Step", "201141":"Deflect_Counter_Step_L",
    "201142":"Deflect_Counter_Step_R",
    "201200":"Deflect_Receive_01", "201210":"Deflect_Receive_02",
    "201300":"Guard_Heavy_Hit", "201301":"Guard_Heavy_Hit_L",
    "201302":"Guard_Heavy_Hit_R", "201303":"Guard_Heavy_Hit_Fwd",
    "201304":"Guard_Heavy_Hit_Back", "201305":"Guard_Heavy_Hit_KnockBack",
    "201320":"Guard_Break_Recover", "201321":"Guard_Break_Recover_Fast",
    "201500":"Guard_Counter", "201501":"Guard_Counter_Alt",
    "201600":"Deflect_Jump", "201610":"Deflect_Jump_Receive",
    "202000":"Parry_Recover", "202010":"Parry_Recover_L",
    "202100":"Parry_Into_Guard", "202300":"Guard_Into_Parry",
    "202400":"Guard_Into_Attack", "202600":"Guard_Break", "202610":"Guard_Break_L",
    "202700":"Guard_Break_Attack", "202710":"Guard_Break_Attack_L",
    "205010":"Mikiri_Counter", "205011":"Mikiri_Counter_Alt",
    "205020":"Mikiri_StepIn", "205030":"Mikiri_Stab",
    "206100":"Mikiri_Receive", "206101":"Mikiri_Receive_Alt",
    "206120":"Mikiri_Recover",
    "210000":"PostureBreak_Receive", "210001":"PostureBreak_Receive_Heavy",
    "210010":"PostureBreak_Stagger", "210018":"PostureBreak_Fall",
    "210050":"PostureBreak_Recover", "210100":"PostureBreak_KnockDown",
    "210150":"PostureBreak_KnockDown_Recover",
    "213100":"Sweep_JumpKick", "213110":"Sweep_JumpKick_L",
    "213301":"Sweep_Jump", "213302":"Sweep_Jump_L",
    "213303":"Sweep_Jump_R", "213304":"Sweep_Jump_Bwd",
    "216000":"Grab_Receive", "216010":"Grab_Receive_Alt",
    "216020":"Grab_Escape", "216100":"Grab_Throw_Receive",
    "220000":"Deathblow_Front", "220001":"Deathblow_Back", "220002":"Deathblow_Air",
    "220003":"Deathblow_Plunge", "220004":"Deathblow_Ledge",
    "220005":"Deathblow_Climb", "220006":"Deathblow_Sneak",
    "220010":"Deathblow_Front_Receive", "220011":"Deathblow_Back_Receive",
    "220012":"Deathblow_Air_Receive", "220020":"Deathblow_Front_Alt",
    "220021":"Deathblow_Back_Alt", "220022":"Deathblow_Air_Alt",
    "250000":"Hit_Light_Front", "250001":"Hit_Light_Back",
    "250005":"Hit_Light_L", "250006":"Hit_Light_R",
    "250010":"Hit_Medium_Front", "250011":"Hit_Medium_Back",
    "250015":"Hit_Medium_L", "250016":"Hit_Medium_R",
    "250030":"Hit_Heavy_Front", "250031":"Hit_Heavy_Back",
    "250035":"Hit_Heavy_L", "250036":"Hit_Heavy_R",
    "250040":"Hit_KnockBack", "250041":"Hit_KnockBack_Heavy",
    "250045":"Hit_KnockBack_L", "250046":"Hit_KnockBack_R",
    "250100":"Hit_Stagger", "250110":"Hit_Stagger_Heavy",
    "250200":"Hit_Ground_Recover", "250300":"Hit_Air_Recover",
    "250400":"Hit_HeadShot", "250500":"Hit_Poison", "250600":"Hit_Terror",
    "250610":"Hit_Terror_Death",
    "251000":"Death_Front", "251100":"Death_Back", "251200":"Death_L", "251300":"Death_R",
    "251400":"Death_Fall", "251410":"Death_Fall_Long",
    "251500":"Death_Special", "251530":"Death_Immortal",
    "251540":"Death_Immortal_Fall", "251550":"Death_Immortal_Recover",
    "251600":"Death_Plunge", "251800":"Death_Grab", "252000":"Death_Snake",
    "259000":"Resurrect_01", "259010":"Resurrect_02",
    "700010":"Prosthetic_Shuriken", "700200":"Prosthetic_Shuriken_Charged",
    "700240":"Prosthetic_Shuriken_Jump", "700280":"Prosthetic_Shuriken_Sprint",
    "700300":"Prosthetic_Shuriken_Slide", "700500":"Prosthetic_Shuriken_Dash",
    "710000":"Prosthetic_Axe", "710100":"Prosthetic_Axe_Charged",
    "710200":"Prosthetic_Axe_Jump", "710310":"Prosthetic_Axe_Dash",
    "710400":"Prosthetic_Spear", "710410":"Prosthetic_Spear_Charged",
    "710420":"Prosthetic_Spear_Jump", "710430":"Prosthetic_Spear_Dash",
    "710500":"Prosthetic_FlameVent", "710510":"Prosthetic_FlameVent_Charged",
    "710520":"Prosthetic_FlameVent_Jump", "710530":"Prosthetic_FlameVent_Dash",
    "710600":"Prosthetic_Umbrella", "710700":"Prosthetic_Umbrella_Open",
    "710800":"Prosthetic_Umbrella_Spin", "710900":"Prosthetic_Umbrella_Attack",
    "711000":"Prosthetic_Sabimaru", "711010":"Prosthetic_Sabimaru_Combo",
    "711020":"Prosthetic_Sabimaru_Dash",
    "711100":"Prosthetic_Whistle", "711110":"Prosthetic_Whistle_Charged",
    "711200":"Prosthetic_Firecracker", "711201":"Prosthetic_Firecracker_Alt",
    "711210":"Prosthetic_Firecracker_Dash", "711211":"Prosthetic_Firecracker_Dash_Alt",
    "711300":"Prosthetic_MistRaven",
    "711310":"Prosthetic_MistRaven_Fwd", "711311":"Prosthetic_MistRaven_L",
    "711312":"Prosthetic_MistRaven_R", "711315":"Prosthetic_MistRaven_Bwd",
    "711400":"Prosthetic_FingerWhistle",
    "711500":"Prosthetic_DivineAbduction", "711510":"Prosthetic_DivineAbduction_Charged",
    "790000":"Grapple_Start", "790010":"Grapple_Fly", "790020":"Grapple_Land",
    "790030":"Grapple_Ledge", "790040":"Grapple_Vault",
    "790050":"Grapple_Swing", "790060":"Grapple_Swing_L",
    "790070":"Grapple_Swing_R", "790080":"Grapple_Swing_End",
    "790090":"Grapple_Attack", "790100":"Grapple_Attack_L",
    "790110":"Grapple_Attack_R", "790120":"Grapple_Attack_Fwd",
    "790130":"Grapple_Attack_Bwd", "790500":"Grapple_To_Ledge",
})


def extract_json_value(text, start_pos, is_array=False):
    """Extract a JSON object or array starting at start_pos (after opening '{' or '[').
    Returns (json_string, end_pos) or (None, -1) on failure."""
    open_ch = '[' if is_array else '{'
    close_ch = ']' if is_array else '}'
    depth = 1
    i = start_pos
    in_string = False
    escape = False

    while i < len(text) and depth > 0:
        ch = text[i]
        if escape:
            escape = False
        elif ch == '\\':
            escape = True
        elif ch == '"':
            in_string = not in_string
        elif not in_string:
            if ch == '{' or ch == '[':
                depth += 1
            elif ch == '}' or ch == ']':
                depth -= 1
        i += 1

    if depth == 0:
        return open_ch + text[start_pos:i], i
    return None, -1


def extract_json_object(text, start_pos):
    """Extract a JSON object string starting at start_pos (after the opening '{')."""
    return extract_json_value(text, start_pos, is_array=False)


def extract_json_array(text, start_pos):
    """Extract a JSON array string starting at start_pos (after the opening '[')."""
    return extract_json_value(text, start_pos, is_array=True)


def main():
    t0 = time.time()
    parser = argparse.ArgumentParser(description="Streaming Sekiro animation extractor")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--output", type=str, default=DST)
    args = parser.parse_args()

    wanted_ids = set(ANIMATION_MAP.keys())
    print(f"Target: {len(wanted_ids)} mapped animation IDs")

    # ========================================================================
    # Step 1: Read the file in chunks and extract matching animation blocks
    # ========================================================================
    file_size = os.path.getsize(SRC)
    print(f"Source: {SRC} ({file_size / (1024*1024):.0f} MB)")

    selected_anims = []       # list of (id, readable_name, json_string)
    matched_ids = set()
    header_data = None

    # Build pattern to match animation names in the wanted set
    # We'll scan for {"Name":"Sekiro_a000_XXXXXX" patterns

    # Read the entire file as binary for fast scanning
    print("Phase 1: Scanning for matching animations...")
    with open(SRC, "rb") as f:
        raw = f.read()

    text = raw.decode("utf-8", errors="ignore")
    del raw

    # Extract header data
    bn_match = re.search(r'"BoneNames":(\[[^\]]+\])', text)
    bone_names = json.loads(bn_match.group(1)) if bn_match else []
    bp_match = re.search(r'"BoneParents":(\[[^\]]+\])', text)
    bone_parents = json.loads(bp_match.group(1)) if bp_match else []
    bc_match = re.search(r'"BoneCount":(\d+)', text)
    bone_count = int(bc_match.group(1)) if bc_match else 0

    # Try to extract BoneLocalTransforms (use delimiter-based approach)
    blt_start = text.find('"BoneLocalTransforms":[')
    bone_transforms = []
    if blt_start >= 0:
        # Find the end of the BoneLocalTransforms array by looking for next field
        arr_content_start = blt_start + len('"BoneLocalTransforms":')
        # Search for the "AnimationCount" field which comes after BoneLocalTransforms
        next_field = text.find(',"AnimationCount":', arr_content_start)
        if next_field < 0:
            next_field = text.find('"AnimationCount":', arr_content_start)
        if next_field >= 0:
            arr_str = text[arr_content_start:next_field]
            try:
                bone_transforms = json.loads(arr_str)
                print(f"  BoneLocalTransforms: {len(bone_transforms)} bones")
            except json.JSONDecodeError as e:
                print(f"  Warning: Failed to parse BoneLocalTransforms: {e}")
                print(f"  Content ends with: ...{arr_str[-100:]}")
        if not bone_transforms:
            # Fallback: use bracket matching
            arr_str2, _ = extract_json_array(text, arr_content_start)
            if arr_str2:
                try:
                    bone_transforms = json.loads(arr_str2)
                    print(f"  BoneLocalTransforms (fallback): {len(bone_transforms)} bones")
                except json.JSONDecodeError:
                    pass
        if not bone_transforms:
            print("  Warning: BoneLocalTransforms not available (will be empty)")

    print(f"  BoneCount: {bone_count}, BoneNames: {len(bone_names)}")

    # Find all animation objects in the text
    print("Phase 2: Extracting matching animation objects...")

    # The Animations array starts with "Animations":[
    anim_array_start = text.find('"Animations":[')
    if anim_array_start < 0:
        print("ERROR: Could not find Animations array")
        sys.exit(1)

    # Search from the Animations array start
    search_start = anim_array_start + len('"Animations":[')

    # Find all animation object starts: {"Name":"
    pattern = re.compile(r'\{"Name":"Sekiro_a000_(\d+)"')
    for m in pattern.finditer(text, search_start):
        anim_id = m.group(1)
        if anim_id not in wanted_ids or anim_id in matched_ids:
            continue

        # Extract the full JSON object
        obj_start = m.start() + 1  # skip the opening '{'
        obj_str, end_pos = extract_json_object(text, obj_start)

        if obj_str:
            matched_ids.add(anim_id)
            selected_anims.append((anim_id, ANIMATION_MAP[anim_id], obj_str))

            if len(matched_ids) % 50 == 0:
                print(f"  Found {len(matched_ids)}/{len(wanted_ids)}...")

    print(f"  Extracted {len(matched_ids)}/{len(wanted_ids)} animations")

    # ========================================================================
    # Step 3: Parse extracted JSON objects and rename
    # ========================================================================
    print("Phase 3: Parsing and renaming animations...")
    animations = []

    for anim_id, readable, obj_str in selected_anims:
        try:
            anim = json.loads(obj_str)
            anim["OriginalName"] = f"Sekiro_a000_{anim_id}"
            anim["Name"] = f"Sekiro_{readable}"
            animations.append(anim)
        except json.JSONDecodeError as e:
            print(f"  Warning: Failed to parse {anim_id}: {e}")

    animations.sort(key=lambda a: a["Name"])

    # ========================================================================
    # Step 4: Summary
    # ========================================================================
    print(f"\n{'='*60}")
    print(f"Selected: {len(animations)} animations")
    print(f"{'='*60}")

    cat_summary = {}
    for a in animations:
        parts = a["Name"].split("_")
        cat = parts[1] if len(parts) >= 2 else "Other"
        cat_summary[cat] = cat_summary.get(cat, 0) + 1

    print("By category:")
    for cat, count in sorted(cat_summary.items(), key=lambda x: -x[1]):
        print(f"  {cat}: {count}")

    missing = wanted_ids - matched_ids
    if missing:
        print(f"\nMissing IDs ({len(missing)}): {sorted(missing)[:20]}...")

    if args.dry_run:
        print("\nDRY RUN - no file written")
        print("\nAnimation list:")
        for a in animations:
            print(f"  {a['OriginalName']} → {a['Name']} ({a.get('FrameCount','?')}f)")
        return

    # ========================================================================
    # Step 5: Write output
    # ========================================================================
    out = {
        "BoneCount": bone_count,
        "BoneNames": bone_names,
        "BoneParents": bone_parents,
        "BoneLocalTransforms": bone_transforms,
        "AnimationCount": len(animations),
        "Animations": animations,
    }

    print(f"\nWriting to {args.output}...")
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(out, f, separators=(",", ":"))

    out_size = os.path.getsize(args.output) / (1024 * 1024)
    elapsed = time.time() - t0
    print(f"Done! {out_size:.1f} MB in {elapsed:.1f}s")


if __name__ == "__main__":
    main()
