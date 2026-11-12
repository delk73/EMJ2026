"""Rendering-enabled commandlet probe of the actual Canvas UI-material path; not viewport QA."""
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
parent = unreal.load_asset("/Game/Materials/M_TrayGlyphSDF")
material = unreal.MaterialLibrary.create_dynamic_material_instance(world,parent)
target = unreal.RenderingLibrary.create_render_target2d(world,128,128,unreal.TextureRenderTargetFormat.RTF_RGBA16F)
signatures = []
for glyph in range(3):
    material.set_scalar_parameter_value("GlyphIndex",glyph)
    unreal.RenderingLibrary.clear_render_target2d(world,target,unreal.LinearColor(0,0,0,0))
    unreal.RenderingLibrary.draw_material_to_render_target(world,target,material)
    core = unreal.RenderingLibrary.read_render_target_raw_pixel(world,target,64,64)
    corner = unreal.RenderingLibrary.read_render_target_raw_pixel(world,target,0,0)
    assert core.r>0.1 and abs(core.r-core.g)<0.01 and abs(core.g-core.b)<0.01, "Canvas SDF glyph failed: " + str(core)
    assert corner.r<0.01, "Transparent SDF background failed: " + str(corner)
    signature = [unreal.RenderingLibrary.read_render_target_raw_pixel(world,target,x,y).r>0.1
                 for x,y in ((90,46),(27,64),(64,32))]
    assert signature == [glyph==i for i in range(3)], (glyph,signature)
    signatures.append(signature)
baseline = core.r
material.set_scalar_parameter_value("HoverAlpha",1)
material.set_scalar_parameter_value("DragAlpha",1)
unreal.RenderingLibrary.clear_render_target2d(world,target,unreal.LinearColor(0,0,0,0))
unreal.RenderingLibrary.draw_material_to_render_target(world,target,material)
brightened = unreal.RenderingLibrary.read_render_target_raw_pixel(world,target,64,64).r
assert brightened>baseline+0.1, (baseline,brightened)
unreal.log("TRAY_CANVAS_GPU_PROBE_OK: three distinct SDF silhouettes="+str(signatures)+
           " greyscale cores, transparent corners, hover/drag core "+str(baseline)+" -> "+str(brightened))
