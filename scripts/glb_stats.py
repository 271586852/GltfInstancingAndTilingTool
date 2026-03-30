#!/usr/bin/env python3
import argparse
import csv
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional


GLB_MAGIC = b"glTF"
JSON_CHUNK_TYPE = 0x4E4F534A  # "JSON"
INSTANCING_EXT = "EXT_mesh_gpu_instancing"
# You can directly fill your input folder here, e.g. r"D:\data\glb".
DEFAULT_INPUT_DIR = r"D:\dissertationProject\Data\shiyanshuju\MEP\ViewerExport_10_15_Instancing"


@dataclass
class GlbStats:
    file_name: str
    relative_path: str
    sc_bytes: int
    sc_mb: float
    ec: int
    eic: int
    ir: float
    ic: int
    pic: float
    instancer_entity_counts: List[int]


def parse_glb_json(glb_path: Path) -> Dict[str, Any]:
    with glb_path.open("rb") as f:
        header = f.read(12)
        if len(header) != 12:
            raise ValueError("GLB header is incomplete")

        magic, version, total_length = struct.unpack("<4sII", header)
        if magic != GLB_MAGIC:
            raise ValueError("Not a valid GLB file (magic mismatch)")
        if version not in (1, 2):
            raise ValueError(f"Unsupported GLB version: {version}")

        json_chunk: Optional[bytes] = None
        bytes_read = 12

        while bytes_read < total_length:
            chunk_header = f.read(8)
            if len(chunk_header) != 8:
                break
            chunk_length, chunk_type = struct.unpack("<II", chunk_header)
            chunk_data = f.read(chunk_length)
            if len(chunk_data) != chunk_length:
                raise ValueError("GLB chunk data is incomplete")
            bytes_read += 8 + chunk_length

            if chunk_type == JSON_CHUNK_TYPE and json_chunk is None:
                json_chunk = chunk_data

        if json_chunk is None:
            raise ValueError("GLB JSON chunk not found")

        json_text = json_chunk.decode("utf-8").rstrip("\x00 \t\r\n")
        return json.loads(json_text)


def get_instance_count_from_node(node: Dict[str, Any], accessors: List[Dict[str, Any]]) -> int:
    extensions = node.get("extensions", {})
    if not isinstance(extensions, dict):
        return 0

    instancing = extensions.get(INSTANCING_EXT)
    if not isinstance(instancing, dict):
        return 0

    attributes = instancing.get("attributes", {})
    if not isinstance(attributes, dict):
        return 0

    # Align with existing C++ logic: use one valid accessor's count as instance count.
    for accessor_id in attributes.values():
        if not isinstance(accessor_id, int):
            continue
        if accessor_id < 0 or accessor_id >= len(accessors):
            continue
        accessor = accessors[accessor_id]
        count = accessor.get("count", 0)
        if isinstance(count, int) and count >= 0:
            return count
    return 0


def compute_stats_for_glb(glb_path: Path, root_dir: Path) -> GlbStats:
    gltf = parse_glb_json(glb_path)
    nodes = gltf.get("nodes", [])
    accessors = gltf.get("accessors", [])

    if not isinstance(nodes, list):
        nodes = []
    if not isinstance(accessors, list):
        accessors = []

    ic = 0
    eic = 0
    instancer_entity_counts: List[int] = []

    for node in nodes:
        if not isinstance(node, dict):
            continue
        extensions = node.get("extensions", {})
        if isinstance(extensions, dict) and INSTANCING_EXT in extensions:
            ic += 1
            instance_count = get_instance_count_from_node(node, accessors)
            eic += instance_count
            instancer_entity_counts.append(instance_count)

    non_instanced_nodes = max(0, len(nodes) - ic)
    # Ec: current entity count without instance expansion
    # Ec = instanced entities (Ic) + non-instanced normal entities
    ec = ic + non_instanced_nodes

    ir = (eic / ec) if ec > 0 else 0.0
    pic = (eic / ic) if ic > 0 else 0.0

    sc_bytes = glb_path.stat().st_size
    sc_mb = sc_bytes / (1024.0 * 1024.0)

    return GlbStats(
        file_name=glb_path.name,
        relative_path=str(glb_path.relative_to(root_dir)),
        sc_bytes=sc_bytes,
        sc_mb=sc_mb,
        ec=ec,
        eic=eic,
        ir=ir,
        ic=ic,
        pic=pic,
        instancer_entity_counts=instancer_entity_counts,
    )


def collect_glb_files(input_dir: Path, recursive: bool) -> List[Path]:
    if recursive:
        candidates = [p for p in input_dir.rglob("*") if p.is_file()]
    else:
        candidates = [p for p in input_dir.iterdir() if p.is_file()]
    glb_files = [p for p in candidates if p.suffix.lower() == ".glb"]
    glb_files.sort(key=lambda p: str(p).lower())
    return glb_files


def write_csv(output_csv: Path, stats_list: List[GlbStats]) -> None:
    total_sc_bytes = sum(s.sc_bytes for s in stats_list)
    total_sc_mb = sum(s.sc_mb for s in stats_list)
    total_ec = sum(s.ec for s in stats_list)
    total_eic = sum(s.eic for s in stats_list)
    total_ic = sum(s.ic for s in stats_list)
    total_ir = (total_eic / total_ec) if total_ec > 0 else 0.0
    total_pic = (total_eic / total_ic) if total_ic > 0 else 0.0

    output_csv.parent.mkdir(parents=True, exist_ok=True)

    with output_csv.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "FileName",
                "RelativePath",
                "SC_bytes",
                "SC_MB",
                "Ec",
                "EIc",
                "IR",
                "Ic",
                "PIC",
                "InstancerEntityCounts",
            ]
        )

        for s in stats_list:
            writer.writerow(
                [
                    s.file_name,
                    s.relative_path,
                    s.sc_bytes,
                    f"{s.sc_mb:.6f}",
                    s.ec,
                    s.eic,
                    f"{s.ir:.6f}",
                    s.ic,
                    f"{s.pic:.6f}",
                    ";".join(str(v) for v in s.instancer_entity_counts),
                ]
            )

        writer.writerow(
            [
                "TOTAL",
                "",
                total_sc_bytes,
                f"{total_sc_mb:.6f}",
                total_ec,
                total_eic,
                f"{total_ir:.6f}",
                total_ic,
                f"{total_pic:.6f}",
                "",
            ]
        )


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Scan GLB files in a folder and export SC/Ec/EIc/IR/Ic/PIC stats to CSV."
    )
    parser.add_argument(
        "--input_dir",
        default=DEFAULT_INPUT_DIR,
        help='Path to directory containing GLB files. If omitted, uses DEFAULT_INPUT_DIR in code.',
    )
    parser.add_argument(
        "--output_csv",
        default="",
        help="Output CSV path. If omitted, exports to <input_dir>/glb_stats_output.csv.",
    )
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Recursively scan subdirectories for GLB files.",
    )
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    if not args.input_dir or not args.input_dir.strip():
        print(
            'Please set input path in code: DEFAULT_INPUT_DIR = r"..." or pass --input_dir.',
            file=sys.stderr,
        )
        return 1

    input_dir = Path(args.input_dir).resolve()
    output_csv = (
        Path(args.output_csv).resolve()
        if args.output_csv and args.output_csv.strip()
        else (input_dir / "glb_stats_output.csv")
    )

    if not input_dir.exists() or not input_dir.is_dir():
        print(f"Input directory does not exist or is not a directory: {input_dir}", file=sys.stderr)
        return 1

    glb_files = collect_glb_files(input_dir, args.recursive)
    if not glb_files:
        print(f"No GLB files found in: {input_dir}", file=sys.stderr)
        return 1

    stats_list: List[GlbStats] = []
    failed_files = 0

    for glb_file in glb_files:
        try:
            stats = compute_stats_for_glb(glb_file, input_dir)
            stats_list.append(stats)
        except Exception as ex:
            failed_files += 1
            print(f"[WARN] Failed to parse {glb_file}: {ex}", file=sys.stderr)

    if not stats_list:
        print("No valid GLB files parsed successfully.", file=sys.stderr)
        return 1

    write_csv(output_csv, stats_list)

    print(f"Processed GLB files: {len(stats_list)}")
    if failed_files:
        print(f"Failed files: {failed_files}")
    print(f"CSV written to: {output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
