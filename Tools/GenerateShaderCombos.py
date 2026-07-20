#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_DIR = ROOT / "ERRender" / "Source" / "Generated"

DEFAULT_SHADERS = [
    ROOT / "Resources" / "Shaders" / "Skin.hlsl",
    ROOT / "Resources" / "Shaders" / "ShadowDepth.hlsl",
    ROOT / "Resources" / "Shaders" / "World.hlsl",
    ROOT / "Resources" / "Shaders" / "Grass.hlsl",
    ROOT / "Resources" / "Shaders" / "ScreenSpace.hlsl",
    ROOT / "Resources" / "Shaders" / "System.hlsl",
    ROOT / "Resources" / "Shaders" / "UI.hlsl",
    ROOT / "Resources" / "Shaders" / "SkyMask.hlsl",
    ROOT / "Resources" / "Shaders" / "SunShafts.hlsl",
    ROOT / "Resources" / "Shaders" / "CopyTexture.hlsl",
    ROOT / "Resources" / "Shaders" / "Postprocess.hlsl",
    ROOT / "Resources" / "Shaders" / "FXAA.hlsl",
    ROOT / "Resources" / "Shaders" / "SMAAEdgeDetection.hlsl",
    ROOT / "Resources" / "Shaders" / "SMAABlendWeight.hlsl",
    ROOT / "Resources" / "Shaders" / "SMAANeighborhood.hlsl",
    ROOT / "Resources" / "Shaders" / "ResolveDepth.hlsl",
    ROOT / "Resources" / "Shaders" / "DownsampleDepthMin.hlsl",
    ROOT / "Resources" / "Shaders" / "DualKawaseDown.hlsl",
    ROOT / "Resources" / "Shaders" / "DualKawaseUp.hlsl",
    ROOT / "Resources" / "Shaders" / "HDRBloomThreshold.hlsl",
    ROOT / "Resources" / "Shaders" / "GlowBloomComposite.hlsl",
    ROOT / "Resources" / "Shaders" / "HBAOPlus.hlsl",
    ROOT / "Resources" / "Shaders" / "XeGTAO.hlsl",
    ROOT / "Resources" / "Shaders" / "SSR.hlsl",
    ROOT / "Resources" / "Shaders" / "HBAOBlur.hlsl",
    ROOT / "Resources" / "Shaders" / "HBAOComposite.hlsl",
    ROOT / "Resources" / "Shaders" / "VolumetricFog.hlsl",
    ROOT / "Resources" / "Shaders" / "VolumetricFogComposite.hlsl",
    ROOT / "Resources" / "Shaders" / "CloudShadow.hlsl",
]

AGGREGATE_HEADER_NAME = "ShaderCombos.h"

COMBO_RE = re.compile(r'^\s*//\s*(STATIC|DYNAMIC):\s*"([^"]+)"\s*"(-?\d+)\.\.(-?\d+)"')
ENTRYPOINT_RE = re.compile(r'^\s*[A-Za-z_][A-Za-z0-9_<>]*\s+((?:vs|ps)_[A-Za-z0-9_]+)\s*\(')


def parse_args():
    parser = argparse.ArgumentParser(description="Generate C++ shader combo headers from HLSL combo comments.")
    parser.add_argument("shaders", nargs="*", type=Path, help="Shader files to parse. Defaults to all remaster shaders.")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR, help="Directory for generated headers.")
    return parser.parse_args()


def parse_combos(path: Path):
    combos = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("//"):
            break

        match = COMBO_RE.match(line)
        if not match:
            continue

        _, name, min_value, max_value = match.groups()
        min_value = int(min_value)
        max_value = int(max_value)
        if min_value > max_value:
            raise ValueError(f"{path}: invalid combo range for {name}")

        combos.append((name, min_value, max_value))

    return combos


def parse_entrypoints(path: Path):
    entrypoints = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = ENTRYPOINT_RE.match(line)
        if not match:
            continue

        entrypoint = match.group(1)
        if entrypoint not in entrypoints:
            entrypoints.append(entrypoint)

    return entrypoints


def sanitize(name: str):
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if sanitized and sanitized[0].isdigit():
        sanitized = f"_{sanitized}"
    return sanitized


def shader_name(path: Path):
    return sanitize(path.stem)


def header_name(path: Path):
    return f"{shader_name(path)}ShaderCombos.h"


def runtime_shader_path(path: Path):
    return f'Data\\\\Shaders\\\\{path.name}'


def emit_shader(out, name: str, shader_path: Path, combos):
    out.append(f"enum {name}ComboFlags : TUINT")
    out.append("{")

    if combos:
        for index, (combo_name, _, _) in enumerate(combos):
            out.append(f"\t{name}_{sanitize(combo_name)} = BITFLAG( {index} ),")
    else:
        out.append(f"\t{name}_NoCombos = 0,")

    out.append("};")
    out.append("")

    stride = 1
    ranges = []
    for combo_name, min_value, max_value in combos:
        ranges.append((combo_name, min_value, max_value, stride))
        stride *= max_value - min_value + 1

    if ranges:
        out.append(f"static constexpr dx11::ShaderComboDefinition {name}Combos[] =")
        out.append("{")

        for combo_name, min_value, max_value, combo_stride in ranges:
            out.append(f'\t{{ "{combo_name}", {min_value}, {max_value}, {combo_stride} }},')

        out.append("};")
    else:
        out.append(f"static constexpr const dx11::ShaderComboDefinition* {name}Combos = TNULL;")

    out.append("")
    out.append(f"static constexpr TUINT {name}NumCombos = {len(ranges)};")
    out.append(f"static constexpr TUINT {name}NumPermutations = {stride};")
    out.append("")
    out.append(f"TINLINE TUINT Get{name}ComboIndex( TUINT a_uiComboFlags )")
    out.append("{")
    out.append("\tTUINT uiIndex = 0;")

    for combo_name, min_value, max_value, combo_stride in ranges:
        value = f"( ( a_uiComboFlags & {name}_{sanitize(combo_name)} ) ? 1U : 0U )"
        if min_value != 0 or max_value != 1:
            out.append(f"\t// {combo_name} uses range {min_value}..{max_value}; boolean flags select min/max.")
            value = f"( ( a_uiComboFlags & {name}_{sanitize(combo_name)} ) ? {max_value}U : {min_value}U )"

        out.append(f"\tuiIndex += ( {value} - {min_value}U ) * {combo_stride}U;")

    out.append("\treturn uiIndex;")
    out.append("}")
    out.append("")

    out.append(f"TINLINE TBOOL Create{name}ShaderPipelines(")
    out.append("\tdx11::ShaderCombo& a_rVertexShaderCombo,")
    out.append("\tdx11::ShaderCombo* a_pPixelShaderCombo,")
    out.append("\tID3D11InputLayout* a_pInputLayout,")
    out.append("\tToshi::T2DynamicVector<RenderDX11::ShaderPipelineState>& a_rPipelines,")
    out.append("\tconst TCHAR* a_pchNamePrefix")
    out.append(")")
    out.append("{")
    out.append("\tconst TUINT uiNumPermutations = a_rVertexShaderCombo.GetNumPermutations();")
    out.append("\tif ( a_pPixelShaderCombo )")
    out.append("\t{")
    out.append("\t\tTASSERT( a_pPixelShaderCombo->GetNumPermutations() == uiNumPermutations );")
    out.append("\t\tif ( a_pPixelShaderCombo->GetNumPermutations() != uiNumPermutations )")
    out.append("\t\t\treturn TFALSE;")
    out.append("\t}")
    out.append("")
    out.append("\ta_rPipelines.SetSize( uiNumPermutations );")
    out.append("\tfor ( TINT i = 0; i < a_rPipelines.Size(); i++ )")
    out.append("\t{")
    out.append("\t\tRenderDX11::ShaderPipelineState& rPipeline = a_rPipelines[ i ];")
    out.append("\t\trPipeline.pInputLayout = a_pInputLayout;")
    out.append("\t\trPipeline.ppVertexShader = a_rVertexShaderCombo.GetVertexShaderPtr( i );")
    out.append("\t\trPipeline.ppPixelShader = TNULL;")
    out.append("")
    out.append("\t\tTVALIDPTR( rPipeline.ppVertexShader );")
    out.append("\t\tif ( !rPipeline.ppVertexShader || !*rPipeline.ppVertexShader )")
    out.append("\t\t\treturn TFALSE;")
    out.append("")
    out.append("\t\tif ( a_pPixelShaderCombo )")
    out.append("\t\t{")
    out.append("\t\t\trPipeline.ppPixelShader = a_pPixelShaderCombo->GetPixelShaderPtr( i );")
    out.append("\t\t\tTVALIDPTR( rPipeline.ppPixelShader );")
    out.append("\t\t\tif ( !rPipeline.ppPixelShader || !*rPipeline.ppPixelShader )")
    out.append("\t\t\t\treturn TFALSE;")
    out.append("\t\t}")
    out.append("")
    out.append('\t\trPipeline.SetName( Toshi::TString8::VarArgs( "%s_%u", a_pchNamePrefix, i ) );')
    out.append("\t}")
    out.append("")
    out.append("\treturn TTRUE;")
    out.append("}")
    out.append("")

    entrypoints = parse_entrypoints(shader_path)

    for entrypoint in entrypoints:
        stage_name = "Vertex" if entrypoint.startswith("vs_") else "Pixel"
        target = "vs_5_0" if entrypoint.startswith("vs_") else "ps_5_0"
        entrypoint_suffix = sanitize(entrypoint)
        variable_name = f"g_o{name}{stage_name}ShaderCombo_{entrypoint_suffix}"
        compiled_name = f"g_b{name}{stage_name}ShaderComboCompiled_{entrypoint_suffix}"

        out.append(f"inline dx11::ShaderCombo {variable_name};")
        out.append(f"inline TBOOL {compiled_name} = TFALSE;")
        out.append("")
        out.append(f"TINLINE TBOOL Ensure{name}{stage_name}ShaderCombo_{entrypoint_suffix}()")
        out.append("{")
        out.append(f"\tif ( !{compiled_name} )")
        out.append(f'\t\t{compiled_name} = {variable_name}.CompileFromFile( "{runtime_shader_path(shader_path)}", "{entrypoint}", "{target}", {name}Combos, {name}NumCombos, {name}NumPermutations );')
        out.append("")
        out.append(f"\treturn {compiled_name};")
        out.append("}")
        out.append("")
        out.append(f"TINLINE dx11::ShaderCombo& Get{name}{stage_name}ShaderCombo_{entrypoint_suffix}()")
        out.append("{")
        out.append(f"\tTASSERT( {compiled_name} );")
        out.append(f"\treturn {variable_name};")
        out.append("}")
        out.append("")

        shader_type = "ID3D11VertexShader" if stage_name == "Vertex" else "ID3D11PixelShader"
        create_function = "CreateVertexShader" if stage_name == "Vertex" else "CreatePixelShader"
        out.append(f"TINLINE TBOOL Create{name}{stage_name}Shader_{entrypoint_suffix}( {shader_type}** a_ppShader )")
        out.append("{")
        out.append(f"\tif ( !{compiled_name} )")
        out.append("\t\treturn TFALSE;")
        out.append("")
        out.append(f"\tdx11::ShaderCombo& rCombo = {variable_name};")
        shader_getter = "GetVertexShader" if stage_name == "Vertex" else "GetPixelShader"
        shader_ptr_getter = "GetVertexShaderPtr" if stage_name == "Vertex" else "GetPixelShaderPtr"
        out.append(f"\t{shader_type}** ppShader = rCombo.{shader_ptr_getter}( 0 );")
        out.append("\tTVALIDPTR( ppShader );")
        out.append("\tif ( !ppShader || !*ppShader )")
        out.append("\t\treturn TFALSE;")
        out.append("\t*a_ppShader = *ppShader;")
        out.append("\treturn TTRUE;")
        out.append("}")
        out.append("")

    out.append(f"static constexpr TUINT {name}NumWarmupShaders = {name}NumPermutations * {len(entrypoints)}u;")
    out.append("")

    out.append(f"TINLINE TBOOL Prepare{name}ShaderCombos()")
    out.append("{")

    if entrypoints:
        for entrypoint in entrypoints:
            stage_name = "Vertex" if entrypoint.startswith("vs_") else "Pixel"
            entrypoint_suffix = sanitize(entrypoint)
            out.append(f"\tif ( !Ensure{name}{stage_name}ShaderCombo_{entrypoint_suffix}() )")
            out.append("\t\treturn TFALSE;")
            out.append("")

    out.append("\treturn TTRUE;")
    out.append("}")
    out.append("")

    out.append(f"TINLINE TBOOL Compile{name}ShaderCombos()")
    out.append("{")

    if entrypoints:
        for entrypoint in entrypoints:
            stage_name = "Vertex" if entrypoint.startswith("vs_") else "Pixel"
            entrypoint_suffix = sanitize(entrypoint)
            variable_name = f"g_o{name}{stage_name}ShaderCombo_{entrypoint_suffix}"
            create_method = "CreateVertexShaders" if stage_name == "Vertex" else "CreatePixelShaders"
            out.append(f"\tif ( !Ensure{name}{stage_name}ShaderCombo_{entrypoint_suffix}() )")
            out.append("\t\treturn TFALSE;")
            out.append("")
            out.append(f"\tif ( !{variable_name}.{create_method}() )")
            out.append("\t\treturn TFALSE;")
    else:
        out.append("\treturn TTRUE;")

    if entrypoints:
        out.append("")
        out.append("\treturn TTRUE;")

    out.append("}")
    out.append("")


def generate(shader_path: Path, out_dir: Path):
    shader_path = shader_path.resolve()
    name = shader_name(shader_path)
    lines = [
        "#pragma once",
        "// This file is generated by Tools/GenerateShaderCombos.py. Do not edit manually.",
        '#include "RenderDX11Utils.h"',
        "",
        "namespace remaster",
        "{",
        "namespace shadercombos",
        "{",
        "",
    ]

    emit_shader(lines, name, shader_path, parse_combos(shader_path))

    lines.extend([
        "} // namespace shadercombos",
        "} // namespace remaster",
        "",
    ])

    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / header_name(shader_path)
    out_path.write_text("\n".join(lines), encoding="utf-8")
    return out_path


def generate_aggregate(shader_paths, out_dir: Path):
    lines = [
        "#pragma once",
        "// This file is generated by Tools/GenerateShaderCombos.py. Do not edit manually.",
    ]

    for shader_path in shader_paths:
        lines.append(f'#include "{header_name(shader_path)}"')

    lines.extend([
        "",
        "namespace remaster",
        "{",
        "namespace shadercombos",
        "{",
        "",
        "TINLINE TBOOL CompileAllShaderCombos()",
        "{",
    ])

    for shader_path in shader_paths:
        lines.append(f"\tif ( !Compile{shader_name(shader_path)}ShaderCombos() )")
        lines.append("\t\treturn TFALSE;")

    lines.extend([
        "",
        "\treturn TTRUE;",
        "}",
        "",
        "} // namespace shadercombos",
        "} // namespace remaster",
        "",
    ])

    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / AGGREGATE_HEADER_NAME
    out_path.write_text("\n".join(lines), encoding="utf-8")
    return out_path


def main():
    args = parse_args()
    shaders = args.shaders or DEFAULT_SHADERS
    out_dir = args.out_dir
    if not out_dir.is_absolute():
        out_dir = (Path.cwd() / out_dir).resolve()

    shader_paths = []

    for shader in shaders:
        shader_path = shader if shader.is_absolute() else (Path.cwd() / shader)
        shader_path = shader_path.resolve()
        shader_paths.append(shader_path)
        print(generate(shader_path, out_dir))

    print(generate_aggregate(shader_paths, out_dir))


if __name__ == "__main__":
    main()
