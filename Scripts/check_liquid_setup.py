"""Fast native/map wiring check, not a viewport or input test. Never saves the gym.

Run with -EnablePlugins=PythonScriptPlugin -run=pythonscript -NullRHI
and -script=<absolute path to this script> while the interactive editor is closed.
"""
import unreal
from pathlib import Path

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/AttractionGym")
assert world, "Gym map failed to load"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
boards = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.AttractionSimulation)]
assert len(boards) == 1, "Expected exactly one gym board, found " + str(len(boards))
board = boards[0]
board.reset_simulation()
meshes = board.get_components_by_class(unreal.StaticMeshComponent)
assert len(meshes) == 1, "Expected one flat liquid mesh, found " + str(len(meshes))
assert meshes[0].get_editor_property("static_mesh").get_path_name() == "/Engine/BasicShapes/Plane.Plane"
assert meshes[0].get_material(0), "Liquid material not assigned"
material = unreal.load_asset("/Game/Materials/M_LiquidHeightfield")
assert material and material.get_editor_property("shading_model") == unreal.MaterialShadingModel.MSM_UNLIT
expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
customs = [expression for expression in expressions if isinstance(expression, unreal.MaterialExpressionCustom)]
assert len(customs) == 1, "Expected one analytical Custom HLSL expression"
assert customs[0].get_editor_property("code") == Path(__file__).with_name("LiquidMetaballs.hlsl").read_text()
assert not any(isinstance(expression, unreal.MaterialExpressionTextureSampleParameter2D) for expression in expressions), "Legacy bitmap sample remains"
parameters = {str(expression.get_editor_property("parameter_name")): expression
              for expression in expressions if isinstance(expression, unreal.MaterialExpressionScalarParameter)}
assert set(parameters) == {"NodeCount", "TetherCount", "PlaneHalfExtent", "IsoThreshold", "SurfaceHeight"}
assert parameters["NodeCount"].get_editor_property("default_value") == 0
assert parameters["TetherCount"].get_editor_property("default_value") == 0
assert parameters["PlaneHalfExtent"].get_editor_property("default_value") == 12000
assert board.get_editor_property("plane_half_extent") == 12000
assert board.get_editor_property("board_half_extent") == 1200
scale = meshes[0].get_editor_property("relative_scale3d")
assert scale.x == 240 and scale.y == 240, "Backdrop must be 24000x24000"
count = len(board.get_editor_property("nodes"))
assert board.drop_node(unreal.AttractionNode(), unreal.Vector(400, 200, 0)) == count
assert len(board.get_editor_property("nodes")) == count + 1
board.reset_simulation()
assert len(board.get_editor_property("nodes")) == count
assert not board.slice_tether(-1)
unreal.log("LIQUID_SETUP_OK: 24000x24000 plane, analytical Custom HLSL, no bitmap sample, drop/reset; initial nodes=" + str(count))
unreal.log("LIQUID_GYM_TUNING: pull=" + str(board.get_editor_property("field_strength")) +
           " softness=" + str(board.get_editor_property("softening")) +
           " spring rules=" + str(board.get_editor_property("bond_rules")))
