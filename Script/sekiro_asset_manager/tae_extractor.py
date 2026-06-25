"""
只狼 TAE 文件 → JSON 导出器

用法:
    python tae_extractor.py <tae_directory> <output.json>
"""
import argparse
import json
import os
import struct


def read_u64(data, pos):
    return struct.unpack_from('<Q', data, pos)[0], pos + 8


def read_u32(data, pos):
    return struct.unpack_from('<I', data, pos)[0], pos + 4


def read_f32(data, pos):
    return struct.unpack_from('<f', data, pos)[0], pos + 4


class TaeParser:
    def __init__(self, data):
        self.data = data

    def parse_header(self):
        d = self.data
        assert d[0:4] == b'TAE ', 'Not a TAE file'
        version = struct.unpack_from('<I', d, 8)[0]
        assert version == 0x1000D, f'Expected SDT, got 0x{version:x}'

        p = 0x10
        _, p = read_u64(d, p)     # headerSize
        _, p = read_u64(d, p)     # 1
        _, p = read_u64(d, p)     # 0x50
        _, p = read_u64(d, p)     # 0x80
        event_bank, p = read_u64(d, p)  # eventBank
        p += 8                     # zero
        _, p = read_u64(d, p)     # TAE ID
        p += 8                     # 0 or 1 (varies by file)

        # Search for anim_off: scan from current position
        # Look for a uint64 that is (a) in range [0x100, file_size) and
        # (b) followed by a reasonable anim count
        anim_off = 0
        anim_count = 0
        scan_start = p
        for base_off in range(scan_start, min(scan_start + 64, len(d)), 8):
            candidate = struct.unpack_from('<Q', d, base_off)[0]
            if candidate > 0x100 and candidate < len(d):
                for cnt_off in range(base_off + 8, min(base_off + 24, len(d)), 8):
                    cnt = struct.unpack_from('<Q', d, cnt_off)[0]
                    if 1 <= cnt <= 100000:
                        if candidate + 16 <= len(d):
                            try:
                                aid = struct.unpack_from('<Q', d, candidate)[0]
                                eoff = struct.unpack_from('<Q', d, candidate + 8)[0]
                                if eoff > 0x100 and eoff < len(d):
                                    anim_off = candidate
                                    anim_count = cnt
                                    break
                            except:
                                pass
                if anim_off:
                    break

        if anim_off == 0:
            raise ValueError('Could not locate animation table')

        return {'anim_off': anim_off, 'anim_count': anim_count, 'event_bank': event_bank}

    def parse_animations(self, header):
        d = self.data
        p = header['anim_off']
        count = header['anim_count']

        anims = []
        for _ in range(count):
            if p + 16 > len(d):
                break
            aid, p = read_u64(d, p)
            eoff, p = read_u64(d, p)
            if eoff >= len(d):
                continue
            events = self.parse_anim_events(eoff)
            anims.append({'AnimID': aid, 'Events': events})

        return anims

    def parse_anim_events(self, entry_off):
        d = self.data
        p = entry_off

        ev_off_table, p = read_u64(d, p)
        _, p = read_u64(d, p)
        _, p = read_u64(d, p)
        _, p = read_u64(d, p)
        ev_count, p = read_u32(d, p)
        _, p = read_u32(d, p)
        _, p = read_u32(d, p)
        p += 4

        events = []
        # Validate: offset table must be in valid range
        if ev_count == 0 or ev_off_table == 0:
            return events
        if ev_off_table < 0x100 or ev_off_table >= len(d):
            return events
        if ev_off_table + ev_count * 8 > len(d):
            return events

        for i in range(ev_count):
            off_pos = ev_off_table + i * 8
            if off_pos + 8 > len(d):
                break
            ev_data_off, _ = read_u64(d, off_pos)
            if ev_data_off == 0 or ev_data_off >= len(d) or ev_data_off < 0x100:
                continue

            ep = ev_data_off
            ev_type, ep = read_u32(d, ep)

            # Filter out invalid events (type values > 2000 are actually float data from event groups)
            if ev_type > 2000:
                continue

            start_time, ep = read_f32(d, ep)
            end_time, ep = read_f32(d, ep)
            param_raw = d[ep:ep+12]

            params = {}
            if ev_type == 0 and len(param_raw) >= 4:
                jt = struct.unpack_from('<I', param_raw, 0)[0]
                params['JumpTableID'] = jt
                for j in range(2):
                    v = struct.unpack_from('<I', param_raw, 4+j*4)[0]
                    if v != 0:
                        params[f'Data{j}'] = v
            else:
                params['Raw'] = param_raw.hex() if param_raw else ''

            if start_time != start_time or end_time != end_time or abs(start_time) > 10000 or abs(end_time) > 10000:
                continue  # skip NaN/inf events

            events.append({
                'Type': ev_type,
                'StartFrame': int(round(start_time * 30)),
                'EndFrame': int(round(end_time * 30)),
                'Parameters': params,
            })

        return events


def extract_tae_directory(tae_dir):
    files = []
    for root, _, names in os.walk(tae_dir):
        for n in names:
            if n.lower().endswith('.tae'):
                files.append(os.path.join(root, n))

    print(f'Found {len(files)} TAE files')
    result = {'TotalTaeFiles': len(files), 'TAE_Files': []}

    for fp in sorted(files):
        name = os.path.basename(fp)
        print(f'  Parsing: {name}')

        try:
            with open(fp, 'rb') as f:
                dat = f.read()
            parser = TaeParser(dat)
            hdr = parser.parse_header()
            anims = parser.parse_animations(hdr)
            result['TAE_Files'].append({'FileName': name, 'Animations': anims})
            print(f'    -> {len(anims)} anims')

        except Exception as e:
            print(f'    [Error] {e}')
            result['TAE_Files'].append({'FileName': name, 'Animations': [], 'Error': str(e)})

    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tae_directory')
    ap.add_argument('output_json')
    args = ap.parse_args()

    result = extract_tae_directory(args.tae_directory)
    with open(args.output_json, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=2, ensure_ascii=False)

    print(f'\nDone! Output: {args.output_json}')
    return 0


if __name__ == '__main__':
    exit(main())
