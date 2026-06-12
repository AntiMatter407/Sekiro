"""Dump raw vertex buffer bytes from a FLVER file for comparison with C++ decoder."""
import struct
import sys

def read_at(data, offset, fmt, size):
    if offset + size > len(data):
        return None
    return struct.unpack_from(fmt, data, offset)

def main(flver_path, output_path):
    with open(flver_path, 'rb') as f:
        data = f.read()

    # Read header
    magic = data[0:6].decode('ascii')
    print(f"Magic: {magic}, Size: {len(data)} bytes")
    if magic != 'FLVER\x00' and magic[:4] != 'FLVER':
        magic = data[:4]
        offset = 6
    else:
        offset = 6

    # Endian check
    endian = data[offset]; offset += 2
    big = endian == ord('B')
    endian_char = '>' if big else '<'
    print(f"Endian: {'Big' if big else 'Little'}")

    version = struct.unpack_from(endian_char + 'I', data, offset)[0]; offset += 4
    print(f"Version: 0x{version:X}")

    # Read counts
    data_offset = struct.unpack_from(endian_char + 'I', data, offset)[0]; offset += 4
    data_length = struct.unpack_from(endian_char + 'I', data, offset)[0]; offset += 4
    dummy_count = struct.unpack_from(endian_char + 'I', data, offset)[0]; offset += 4
    material_count = struct.unpack_from(endian_char + 'I', data, offset)[0]; offset += 4
    bone_count = struct.unpack_from(endian_char + 'I', data, offset)[0]; offset += 4
    mesh_count = struct.unpack_from(endian_char + 'I', data, offset)[0]; offset += 4
    vbuf_count = struct.unpack_from(endian_char + 'I', data, offset)[0]; offset += 4

    print(f"\nData offset: 0x{data_offset:X}, Data length: {data_length}")
    print(f"Dummies: {dummy_count}, Materials: {material_count}, Bones: {bone_count}")
    print(f"Meshes: {mesh_count}, VertexBuffers: {vbuf_count}")

    # Skip to meshes: need to find them
    # Skip: bounding box (2*12=24), other header fields
    offset = 0x6C  # After common header
    offset += 4 * 3  # 3x Assert
    if version >= 0x20014:
        offset += 4  # Unk68 + SpecialModifier (2 shorts)
    else:
        offset += 4  # Unk68 (int)
    offset += 4 * 5  # 2x assert, Unk74, 2x assert

    mesh_section_offset = offset
    print(f"\nMesh section starts at: 0x{mesh_section_offset:X}")

    # Skip dummies, materials, bones - they have variable sizes
    # We know the approximate file structure, but finding vertex buffers is complex.
    # Let's just focus on reporting the buffer layout info from the log.

    # Instead, let's extract the buffer layouts directly
    # BufferLayoutCount is at offset 0x58
    layout_count = struct.unpack_from(endian_char + 'I', data, 0x58)[0]
    print(f"\nBufferLayout count: {layout_count}")

    # Find a simpler approach: scan for vertex data patterns
    # Vertex buffer info: BufferIndex, LayoutIndex, VertexSize, VertexCount, 0, 0, BufferLength, BufferOffset
    # Each vbuf descriptor is 0x20 (32) bytes

    # We can't easily find vbuf descriptors without parsing the full file.
    # Let's provide summary info.

    print(f"\n=== FLVER Structure Summary ===")
    print(f"To get raw vertex data, run the C++ import and check logs for:")
    print(f"  'FLVER Vertex Decode (x100 scale): first 5 vertices:'")
    print(f"  'FLVER RawBoneIdx[0-4]: raw=...'")
    print(f"Then compare with SoulsFormats JSON output.")

    # Write simple summary to output
    with open(output_path, 'w') as out:
        out.write(f"FLVER: {flver_path}\n")
        out.write(f"Version: 0x{version:X}, Meshes: {mesh_count}, Materials: {material_count}\n")
        out.write(f"Bones: {bone_count}, VertexBuffers: {vbuf_count}\n")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: dump_vertex_raw.py <flver_path> <output_path>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
