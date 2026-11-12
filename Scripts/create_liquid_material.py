"""Regenerate the analytical liquid material; back up the existing asset first.

UnrealEditor-Cmd.exe EMJ2026.uproject -EnablePlugins=PythonScriptPlugin
  -run=pythonscript -script=Scripts/create_liquid_material.py -unattended -nop4
  -AllowCommandletRendering (no -NullRHI: shader compilation needs a renderer)
Only /Game/Materials/M_LiquidHeightfield is rebuilt. No maps are saved.
"""
from pathlib import Path
from datetime import datetime
import shutil
import unreal

root = Path(__file__).resolve().parent.parent
asset_path = "/Game/Materials/M_LiquidHeightfield"
library = unreal.MaterialEditingLibrary
material = unreal.load_asset(asset_path)
if material:
    source = root / "Content/Materials/M_LiquidHeightfield.uasset"
    backup = root / "Saved/Backups" / ("M_LiquidHeightfield-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".uasset")
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, backup)
    unreal.log("LIQUID_MATERIAL_BACKUP: " + str(backup))
    library.delete_all_material_expressions(material)
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_LiquidHeightfield", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
if not material:
    raise RuntimeError("Failed to create/load liquid material")
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", True)

def node(cls, x, y):
    return library.create_material_expression(material, cls, x, y)

def connect(source, output, target, pin):
    if not library.connect_material_expressions(source, output, target, pin):
        raise RuntimeError("Could not connect material pin: " + pin)

# Mesh-local XY maps [-50,50] to [0,1], independent of the built-in Plane UVs.
world = node(unreal.MaterialExpressionWorldPosition, -1000, 0)
local = node(unreal.MaterialExpressionTransformPosition, -800, 0)
local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
connect(world, "", local, "Input")
mask = node(unreal.MaterialExpressionComponentMask, -600, 0)
for channel, enabled in (("r", True), ("g", True), ("b", False), ("a", False)):
    mask.set_editor_property(channel, enabled)
connect(local, "", mask, "Input")
multiply = node(unreal.MaterialExpressionMultiply, -400, 0)
multiply.set_editor_property("const_b", 0.01)
connect(mask, "", multiply, "A")
add = node(unreal.MaterialExpressionAdd, -200, 0)
add.set_editor_property("const_b", 0.5)
connect(multiply, "", add, "A")

custom = node(unreal.MaterialExpressionCustom, 300, 0)
custom.set_editor_property("description", "Analytical liquid field and exact gradient normals")
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom.set_editor_property("code", Path(__file__).with_name("LiquidMetaballs.hlsl").read_text())
names = ["UV", "DataTex", "NodeCount", "TetherCount", "PlaneHalfExtent", "IsoThreshold", "SurfaceHeight"]
inputs = []
for name in names:
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", name)
    inputs.append(custom_input)
custom.set_editor_property("inputs", inputs)
connect(add, "", custom, "UV")
texture = node(unreal.MaterialExpressionTextureObjectParameter, -200, 250)
texture.set_editor_property("parameter_name", "DataTex")
# Static engine fallback is never read when counts are zero. Runtime replaces it with
# nearest, non-sRGB RGBA32F primitive records; no heightfield bitmap is sampled/uploaded.
fallback = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
texture.set_editor_property("texture", fallback)
texture.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
connect(texture, "", custom, "DataTex")
for index, (name, value) in enumerate((("NodeCount", 0.0), ("TetherCount", 0.0),
        ("PlaneHalfExtent", 12000.0), ("IsoThreshold", 0.18), ("SurfaceHeight", 100.0))):
    parameter = node(unreal.MaterialExpressionScalarParameter, -200, 450 + index * 130)
    parameter.set_editor_property("parameter_name", name)
    parameter.set_editor_property("default_value", value)
    connect(parameter, "", custom, name)
if not library.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("Could not connect emissive")
errors = library.recompile_material(material)
if errors:
    raise RuntimeError("Liquid material compile errors: " + str(errors))
if not unreal.EditorAssetLibrary.save_loaded_asset(material):
    raise RuntimeError("Could not save liquid material")
unreal.log("LIQUID_ANALYTICAL_MATERIAL_COMPILED: " + asset_path)
