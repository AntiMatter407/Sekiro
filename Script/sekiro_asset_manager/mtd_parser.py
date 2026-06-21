"""
MTD 二进制文件解析器。

MTD (Material Definition) 是 FromSoftware 的材质定义格式，定义材质使用的着色器、
参数和纹理槽等信息。每个材质引用一个 MTD 文件来描述其渲染属性。

参考源码：
  - SoulsFormats/Formats/MTD.cs
  - SoulsFormats/Formats/MTD/Param.cs
  - SoulsFormats/Formats/MTD/Texture.cs

二进制格式说明：
  MTD 使用自描述的 Block 结构组织数据。每个 Block 的格式如下：
    int32  0                  (固定 0)
    uint32 data_length         (Block 数据部分长度)
    int32  type                (Block 类型标识)
    int32  version             (Block 版本)
    byte   marker              (结束标记)
    byte[3] padding            (对齐填充)

  字符串格式：
    int32   length             (字符串字节长度)
    byte[]  data               (Shift-JIS 编码的字符串数据)
    byte    marker             (结束标记)
    byte[3] padding            (对齐填充)
"""

import struct
import os
from typing import Any, Dict, List, Optional, Tuple


# ======================================================================
# 枚举定义
# ======================================================================

class BlendMode:
    """MTD BlendMode 枚举，对应 g_BlendMode 参数值。"""
    Normal = 0
    TexEdge = 1
    Blend = 2
    Water = 3
    Add = 4
    Sub = 5
    Mul = 6
    AddMul = 7
    SubMul = 8
    WaterWave = 9
    LSNormal = 32
    LSTexEdge = 33
    LSBlend = 34
    LSWater = 35
    LSAdd = 36
    LSSub = 37
    LSMul = 38
    LSAddMul = 39
    LSSubMul = 40
    LSWaterWave = 41

    _NAMES = {
        0: "Normal", 1: "TexEdge", 2: "Blend", 3: "Water", 4: "Add",
        5: "Sub", 6: "Mul", 7: "AddMul", 8: "SubMul", 9: "WaterWave",
        32: "LSNormal", 33: "LSTexEdge", 34: "LSBlend", 35: "LSWater",
        36: "LSAdd", 37: "LSSub", 38: "LSMul", 39: "LSAddMul",
        40: "LSSubMul", 41: "LSWaterWave",
    }

    @staticmethod
    def name(value: int) -> str:
        return BlendMode._NAMES.get(value, f"Unknown_{value}")


class LightingType:
    """MTD LightingType 枚举，对应 g_LightingType 参数值。"""
    None_ = 0
    HemDirDifSpcx3 = 1
    HemEnvDifSpc = 3

    _NAMES = {0: "None", 1: "HemDirDifSpcx3", 3: "HemEnvDifSpc"}

    @staticmethod
    def name(value: int) -> str:
        return LightingType._NAMES.get(value, f"Unknown_{value}")


class ParamType:
    """材质参数类型枚举。"""
    Bool = "bool"
    Int = "int"
    Int2 = "int2"
    Float = "float"
    Float2 = "float2"
    Float3 = "float3"
    Float4 = "float4"


# ======================================================================
# 二进制读取器（MTD 专用，复用 SoulsFormats Block/标记字符串逻辑）
# ======================================================================

class _BinaryReader:
    """MTD 二进制读取器，处理 Block 结构和带标记的字符串。"""

    def __init__(self, data: bytes):
        self._data = data
        self._pos = 0

    @property
    def position(self) -> int:
        return self._pos

    @position.setter
    def position(self, value: int):
        self._pos = value

    @property
    def remaining(self) -> int:
        return len(self._data) - self._pos

    # ---- 基础读取 ----

    def read_int32(self) -> int:
        val = struct.unpack_from("<i", self._data, self._pos)[0]
        self._pos += 4
        return val

    def read_uint32(self) -> int:
        val = struct.unpack_from("<I", self._data, self._pos)[0]
        self._pos += 4
        return val

    def read_byte(self) -> int:
        val = self._data[self._pos]
        self._pos += 1
        return val

    def read_float(self) -> float:
        val = struct.unpack_from("<f", self._data, self._pos)[0]
        self._pos += 4
        return val

    def read_bool(self) -> bool:
        val = struct.unpack_from("<?", self._data, self._pos)[0]
        self._pos += 1
        return val

    def read_bytes(self, count: int) -> bytes:
        if self._pos + count > len(self._data):
            raise ValueError(
                f"读取 {count} 字节超出文件末尾：位置 {self._pos}"
            )
        result = self._data[self._pos:self._pos + count]
        self._pos += count
        return result

    # ---- 对齐 ----

    def pad(self, align: int = 4):
        """对齐到指定字节边界，对应 C# BinaryReaderEx.Pad(align)。"""
        if self._pos % align > 0:
            self._pos += align - (self._pos % align)

    # ---- 断言 ----

    def assert_int32(self, expected: int) -> int:
        val = self.read_int32()
        if val != expected:
            raise ValueError(
                f"断言失败：位置 {self._pos - 4}，期望 {expected}，实际 {val}"
            )
        return val

    def assert_byte(self, expected: int) -> int:
        val = self.read_byte()
        if val != expected:
            raise ValueError(
                f"断言失败：位置 {self._pos - 1}，期望 {expected}，实际 {val}"
            )
        return val

    # ---- 跨步读取（不移动游标） ----

    def get_int32(self, offset: int) -> int:
        saved = self._pos
        self._pos = offset
        val = self.read_int32()
        self._pos = saved
        return val

    # ---- 字符串读取 ----

    def read_shift_jis(self, offset: int) -> str:
        """从指定偏移读取 Shift-JIS 空终止字符串。"""
        saved = self._pos
        self._pos = offset
        raw = bytearray()
        while self._pos < len(self._data):
            b = self.read_byte()
            if b == 0:
                break
            raw.append(b)
        result = raw.decode("shift_jis", errors="replace")
        self._pos = saved
        return result


# ======================================================================
# Block 结构（对应 SoulsFormats MTD.Block）
# ======================================================================

class _Block:
    """MTD 二进制 Block 结构。

    格式：
      int32  0
      uint32 length
      int32  type
      int32  version
      byte   marker
      byte[3] padding
      ... data ...
    """

    @staticmethod
    def read(br: _BinaryReader,
             assert_type: Optional[int] = None,
             assert_version: Optional[int] = None,
             assert_marker: Optional[int] = None) -> "_Block":
        """读取一个 Block 头部。"""
        br.assert_int32(0)
        length = br.read_uint32()
        start = br.position
        block_type = br.assert_int32(assert_type) if assert_type is not None else br.read_int32()
        version = br.assert_int32(assert_version) if assert_version is not None else br.read_int32()
        marker = br.assert_byte(assert_marker) if assert_marker is not None else br.read_byte()
        # 跳过 padding（3 字节对齐）
        br.read_bytes(3)
        return _Block(start, length, block_type, version, marker)

    def __init__(self, start: int, length: int, block_type: int, version: int, marker: int):
        self.start = start
        self.length = length
        self.block_type = block_type
        self.version = version
        self.marker = marker


# ======================================================================
# 标记字符串读取（对应 SoulsFormats MTD.ReadMarkedString）
# ======================================================================

def _read_marked_string(br: _BinaryReader, marker: int) -> str:
    """读取带标记的字符串。

    格式：
      int32  length          (Shift-JIS 字节长度)
      byte[] data            (Shift-JIS 编码的字符串)
      byte   marker
      byte[] padding         (对齐到 4 字节边界)
    """
    length = br.read_int32()
    raw = br.read_bytes(length)
    string = raw.decode("shift_jis", errors="replace")
    br.assert_byte(marker)
    br.pad(4)
    return string


def _assert_marker(br: _BinaryReader, marker: int):
    """断言并跳过标记字节，然后对齐到 4 字节边界（对应 C# AssertMarker，Pad(4)）。"""
    br.assert_byte(marker)
    br.pad(4)


def _read_marker(br: _BinaryReader) -> int:
    """读取标记字节，然后对齐到 4 字节边界（对应 C# ReadMarker，Pad(4)）。"""
    marker = br.read_byte()
    br.pad(4)
    return marker


# ======================================================================
# 数据类
# ======================================================================

class MtdParam:
    """MTD 材质参数。"""

    def __init__(self):
        self.name: str = ""
        self.type: str = "int"
        self.value: Any = 0

    def __repr__(self) -> str:
        return f"MtdParam({self.name}={self.value}, type={self.type})"


class MtdTexture:
    """MTD 纹理槽定义。"""

    def __init__(self):
        self.type: str = ""
        self.extended: bool = False
        self.uv_number: int = 0
        self.shader_data_index: int = 0
        self.path: str = ""
        self.unk_floats: List[float] = []

    def __repr__(self) -> str:
        return f"MtdTexture({self.type}, uv={self.uv_number}, path={self.path})"


# ======================================================================
# 主解析器
# ======================================================================

class MtdParser:
    """
    MTD 二进制文件解析器。

    解析 .mtd 文件中的着色器路径、参数列表和纹理槽定义，
    输出结构化的字典格式供管线使用。
    """

    @staticmethod
    def parse(filepath: str) -> dict:
        """
        解析 MTD 文件。

        Args:
            filepath: MTD 文件路径。

        Returns:
            dict: {
                "ShaderPath": str,       # SPX 着色器路径
                "Description": str,      # 材质描述（可为空）
                "BlendMode": str,        # g_BlendMode 值名称
                "BlendModeValue": int,   # g_BlendMode 数值
                "LightingType": str,     # g_LightingType 值名称
                "LightingTypeValue": int,# g_LightingType 数值
                "MaterialID": int,       # g_GX_MaterialID（若存在则提取）
                "Params": [              # 材质参数完整列表
                    {"Name": str, "Type": str, "Value": any}
                ],
                "Textures": [            # 纹理槽定义
                    {
                        "Type": str,     # 如 "Texture2D_7_AlbedoMap"
                        "Extended": bool,# 是否含扩展信息（只狼专属）
                        "UVNumber": int, # UV 通道编号
                        "ShaderDataIndex": int,
                        "Path": str,     # 默认纹理路径（只狼专属）
                        "UnkFloats": list[float],
                    }
                ],
            }

        Raises:
            FileNotFoundError: 文件不存在。
            ValueError: 文件格式不正确或解析失败。
        """
        if not os.path.exists(filepath):
            raise FileNotFoundError(f"MTD 文件不存在：{filepath}")

        with open(filepath, "rb") as f:
            data = f.read()

        if len(data) < 0x30:
            raise ValueError(f"文件过小，不是有效的 MTD 文件：{filepath}")

        br = _BinaryReader(data)
        return MtdParser._parse_internal(br)

    @staticmethod
    def parse_bytes(data: bytes) -> dict:
        """从字节数据解析 MTD，用于从 BND 中提取的情况。

        Args:
            data: MTD 文件的二进制数据。

        Returns:
            与 parse() 相同结构的字典。
        """
        br = _BinaryReader(data)
        return MtdParser._parse_internal(br)

    @staticmethod
    def _parse_internal(br: _BinaryReader) -> dict:
        """内部解析逻辑。"""
        # ---- 文件 Block (type=0, version=3, marker=0x01) ----
        file_block = _Block.read(br, assert_type=0, assert_version=3, assert_marker=0x01)

        # ---- 头部 Block (type=1, version=2, marker=0xB0) ----
        header_block = _Block.read(br, assert_type=1, assert_version=2, assert_marker=0xB0)

        # 断言标记字符串 "MTD "
        mtd_magic = _read_marked_string(br, 0x34)
        if mtd_magic != "MTD ":
            raise ValueError(f"无效 MTD 魔数：{mtd_magic!r}")

        br.assert_int32(1000)  # 版本号
        _assert_marker(br, 0x01)

        # ---- 数据 Block (type=2, version=4, marker=0xA3) ----
        data_block = _Block.read(br, assert_type=2, assert_version=4, assert_marker=0xA3)

        # ShaderPath: 带标记 0xA3 的字符串
        shader_path = _read_marked_string(br, 0xA3)

        # Description: 带标记 0x03 的字符串
        description = _read_marked_string(br, 0x03)

        br.assert_int32(1)  # 固定值 1

        # ---- 列表 Block (type=3, version=4, marker=0xA3) ----
        lists_block = _Block.read(br, assert_type=3, assert_version=4, assert_marker=0xA3)

        br.assert_int32(0)
        _assert_marker(br, 0x03)

        # ---- 参数列表 ----
        param_count = br.read_int32()
        params = []
        for _ in range(param_count):
            param = MtdParser._read_param(br)
            params.append(param)

        _assert_marker(br, 0x03)

        # ---- 纹理列表 ----
        texture_count = br.read_int32()
        textures = []
        for _ in range(texture_count):
            texture = MtdParser._read_texture(br)
            textures.append(texture)

        _assert_marker(br, 0x04)
        br.assert_int32(0)

        # 关闭列表 Block
        _assert_marker(br, 0x04)
        br.assert_int32(0)

        # 关闭数据 Block
        _assert_marker(br, 0x04)
        br.assert_int32(0)

        # ---- 构建输出 ----
        result = MtdParser._build_output(shader_path, description, params, textures)
        return result

    @staticmethod
    def _read_param(br: _BinaryReader) -> MtdParam:
        """读取一个 Param。"""
        # Param Block (type=4, version=4, marker=0xA3)
        param_block = _Block.read(br, assert_type=4, assert_version=4, assert_marker=0xA3)

        param = MtdParam()
        param.name = _read_marked_string(br, 0xA3)
        type_str = _read_marked_string(br, 0x04)
        param.type = type_str

        br.assert_int32(1)  # 固定值

        # ---- Value Block (type/size/marker 不固定) ----
        # 写入时的 value block 类型取决于 ParamType：
        #   Bool -> type=0x1000, marker=0xC0
        #   Int/Int2 -> type=0x1001, marker=0xC5
        #   Float/Float2/Float3/Float4 -> type=0x1002, marker=0xCA
        # 读取时不需要断言 type/version/marker，只需要正确解析值。
        br.assert_int32(0)
        val_length = br.read_uint32()
        val_start = br.position
        br.read_int32()  # value block type（不断言）
        br.read_int32()  # value block version（不断言）
        _read_marker(br)  # value block marker（不断言）

        # 读取值计数
        value_count = br.read_int32()

        # 根据类型读取值
        param.value = MtdParser._read_param_value(br, param.type, value_count)

        # Value Block 结束标记
        _assert_marker(br, 0x04)
        br.assert_int32(0)

        return param

    @staticmethod
    def _read_param_value(br: _BinaryReader, type_str: str, count: int) -> Any:
        """读取参数值。"""
        if type_str == "bool" or type_str == "Bool":
            return br.read_bool()

        elif type_str == "int" or type_str == "Int":
            return br.read_int32()

        elif type_str == "int2" or type_str == "Int2":
            return [br.read_int32() for _ in range(count)]

        elif type_str == "float" or type_str == "Float":
            return br.read_float()

        elif type_str == "float2" or type_str == "Float2":
            return [br.read_float() for _ in range(count)]

        elif type_str == "float3" or type_str == "Float3":
            return [br.read_float() for _ in range(count)]

        elif type_str == "float4" or type_str == "Float4":
            return [br.read_float() for _ in range(count)]

        else:
            raise NotImplementedError(f"不支持的参数类型：{type_str}")

    @staticmethod
    def _read_texture(br: _BinaryReader) -> MtdTexture:
        """读取一个 Texture。"""
        # Texture Block (type=0x2000, version=3 或 5, marker=0xA3)
        # version=3: 基本格式（不扩展）
        # version=5: 扩展格式（只狼，包含 Path 和 UnkFloats）
        br.assert_int32(0)
        tex_length = br.read_uint32()
        tex_start = br.position
        tex_type = br.assert_int32(0x2000)
        tex_version = br.read_int32()  # 3 或 5
        br.assert_byte(0xA3)
        br.read_bytes(3)  # padding

        tex = MtdTexture()
        tex.extended = (tex_version == 5)
        if tex_version not in (3, 5):
            raise ValueError(f"纹理 Block 版本应为 3 或 5，实际为 {tex_version}")

        tex.type = _read_marked_string(br, 0x35)
        tex.uv_number = br.read_int32()
        _assert_marker(br, 0x35)
        tex.shader_data_index = br.read_int32()

        if tex.extended:
            br.assert_int32(0xA3)
            tex.path = _read_marked_string(br, 0xBA)
            float_count = br.read_int32()
            tex.unk_floats = [br.read_float() for _ in range(float_count)]

        return tex

    @staticmethod
    def _build_output(
        shader_path: str,
        description: str,
        params: List[MtdParam],
        textures: List[MtdTexture],
    ) -> dict:
        """构建最终输出字典。"""
        # 提取关键参数
        blend_mode_value = 0
        lighting_type_value = 0
        material_id = 0

        params_out = []
        for p in params:
            p_dict = {
                "Name": p.name,
                "Type": p.type,
                "Value": p.value,
            }
            params_out.append(p_dict)

            # 提取 g_BlendMode
            if p.name == "g_BlendMode":
                blend_mode_value = int(p.value) if isinstance(p.value, (int, float, bool)) else 0

            # 提取 g_LightingType
            elif p.name == "g_LightingType":
                lighting_type_value = int(p.value) if isinstance(p.value, (int, float, bool)) else 0

            # 提取 g_GX_MaterialID
            elif p.name == "g_GX_MaterialID":
                material_id = int(p.value) if isinstance(p.value, (int, float, bool)) else 0

        textures_out = []
        for t in textures:
            textures_out.append({
                "Type": t.type,
                "Extended": t.extended,
                "UVNumber": t.uv_number,
                "ShaderDataIndex": t.shader_data_index,
                "Path": t.path,
                "UnkFloats": t.unk_floats,
            })

        result = {
            "ShaderPath": shader_path,
            "Description": description,
            "BlendMode": BlendMode.name(blend_mode_value),
            "BlendModeValue": blend_mode_value,
            "LightingType": LightingType.name(lighting_type_value),
            "LightingTypeValue": lighting_type_value,
            "MaterialID": material_id,
            "Params": params_out,
            "Textures": textures_out,
        }

        return result

    # ==================================================================
    # 批量解析 / 查找
    # ==================================================================

    @staticmethod
    def find_and_parse(mtd_name: str, mtd_dir: str) -> dict:
        """在 MTD 目录中查找并解析指定名称的 MTD 文件。

        搜索策略：
          1. 直接拼接 mtd_dir + mtd_name
          2. 在 mtd_dir 的子目录中递归查找文件名匹配的文件

        Args:
            mtd_name: MTD 文件名（如 "P_BD_M_9000_tops1.mtd"）。
            mtd_dir: MTD 根目录。

        Returns:
            解析结果字典，如果文件未找到则返回 None。
        """
        # 直接路径
        direct_path = os.path.join(mtd_dir, mtd_name)
        if os.path.exists(direct_path):
            return MtdParser.parse(direct_path)

        # 提取纯文件名（去掉路径前缀）
        base_name = os.path.basename(mtd_name)

        # 递归搜索
        for root, dirs, files in os.walk(mtd_dir):
            for f in files:
                if f.lower() == base_name.lower():
                    return MtdParser.parse(os.path.join(root, f))

        return None

    @staticmethod
    def parse_all(mtd_dir: str) -> Dict[str, dict]:
        """解析 MTD 目录中的所有 .mtd 文件。

        Args:
            mtd_dir: MTD 文件目录。

        Returns:
            dict: {文件名（不含路径）: 解析结果}
        """
        results = {}
        if not os.path.isdir(mtd_dir):
            return results

        for root, dirs, files in os.walk(mtd_dir):
            for f in files:
                if f.lower().endswith(".mtd"):
                    filepath = os.path.join(root, f)
                    try:
                        results[f] = MtdParser.parse(filepath)
                    except Exception as e:
                        print(f"  [警告] MTD 解析失败 {filepath}: {e}")

        return results

    # ==================================================================
    # BlendMode 到 UE BlendMode 的映射工具
    # ==================================================================

    # MTD BlendMode -> UE BlendMode 映射
    _BLEND_MODE_MAP = {
        "Normal": "Opaque",
        "TexEdge": "Masked",
        "Blend": "Translucent",
        "Water": "Translucent",
        "Add": "Additive",
        "Sub": "Translucent",
        "Mul": "Translucent",
        "AddMul": "Additive",
        "SubMul": "Translucent",
        "WaterWave": "Translucent",
        "LSNormal": "Opaque",
        "LSTexEdge": "Masked",
        "LSBlend": "Translucent",
        "LSWater": "Translucent",
        "LSAdd": "Additive",
        "LSSub": "Translucent",
        "LSMul": "Translucent",
        "LSAddMul": "Additive",
        "LSSubMul": "Translucent",
        "LSWaterWave": "Translucent",
    }

    @staticmethod
    def to_ue_blend_mode(mtd_blend_mode: str) -> str:
        """将 MTD BlendMode 名称映射为 UE 混合模式字符串。

        Args:
            mtd_blend_mode: MTD 中的 BlendMode 名称（如 "TexEdge"）。

        Returns:
            UE 混合模式（"Opaque", "Masked", "Translucent", "Additive"）。
        """
        return MtdParser._BLEND_MODE_MAP.get(mtd_blend_mode, "Opaque")


# ======================================================================
# 命令行入口
# ======================================================================

if __name__ == "__main__":
    import sys
    import json

    if len(sys.argv) < 2:
        print("用法：python mtd_parser.py <MTD文件路径> [输出JSON路径]")
        sys.exit(1)

    filepath = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    try:
        result = MtdParser.parse(filepath)
        output_json = json.dumps(result, indent=2, ensure_ascii=False)

        if output_path:
            with open(output_path, "w", encoding="utf-8") as f:
                f.write(output_json)
            print(f"解析完成，已写入：{output_path}")
        else:
            print(output_json)

        # 打印统计信息
        params = len(result["Params"])
        textures = len(result["Textures"])
        print(f"\n统计：{params} 参数, {textures} 纹理槽")
        print(f"  着色器: {result['ShaderPath']}")
        print(f"  BlendMode: {result['BlendMode']} ({result['BlendModeValue']})")
        print(f"  LightingType: {result['LightingType']} ({result['LightingTypeValue']})")
        print(f"  MaterialID: {result['MaterialID']}")

    except FileNotFoundError as e:
        print(f"错误：{e}", file=sys.stderr)
        sys.exit(1)
    except ValueError as e:
        print(f"解析错误：{e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"未预期的错误：{e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
