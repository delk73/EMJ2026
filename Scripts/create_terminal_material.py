"""Author the owned terminal material and audit every external package before purge."""
from pathlib import Path
import json
import unreal as u
root = Path(__file__).resolve().parent.parent
lib = u.MaterialEditingLibrary
path = '/Game/DirectOrScut/Materials/M_DirectOrScut_Terminal'
m = u.load_asset(path)
if m:
    lib.delete_all_material_expressions(m)
else:
    m = u.AssetToolsHelpers.get_asset_tools().create_asset(path.rsplit('/',1)[1], path.rsplit('/',1)[0], u.Material, u.MaterialFactoryNew())
m.set_editor_property('material_domain',u.MaterialDomain.MD_UI)
m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
def node(cls): return lib.create_material_expression(m,cls)
def link(a,b,pin):
    assert lib.connect_material_expressions(a,'',b,pin),pin
custom=node(u.MaterialExpressionCustom)
custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT4)
params={'WipeProgress':1,'PresentationTimeSeconds':0,'WipeSoftnessPx':1,'ScanlinePitchPx':2,'ScanlineStrength':.12,'DistortionAmount':.005,'DeceptionAmount':0,'CornerRadiusPx':6,'DeceptionSyncTear':0,'ScrubTrackingNoise':0,'SignalOfflineNoise':0,'VoiceBleedIntensity':0}
names=['UV','OpticRenderTarget','ViewportSize','PhosphorTint']+list(params)
inputs=[]
for name in names:
    v=u.CustomInput();v.set_editor_property('input_name',name);inputs.append(v)
custom.set_editor_property('inputs',inputs)
link(node(u.MaterialExpressionTextureCoordinate),custom,'UV')
tex=node(u.MaterialExpressionTextureObjectParameter);tex.set_editor_property('parameter_name','OpticRenderTarget')
tex.set_editor_property('texture',u.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
link(tex,custom,'OpticRenderTarget')
for name,default in [('ViewportSize',u.LinearColor(1024,576,0,0)),('PhosphorTint',u.LinearColor(.65,1,.72,1))]:
    n=node(u.MaterialExpressionVectorParameter);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',default);link(n,custom,name)
for name,default in params.items():
    n=node(u.MaterialExpressionScalarParameter);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',default);link(n,custom,name)
custom.set_editor_property('code','''
float2 size=max(ViewportSize.rg,float2(1,1));
float t=fmod(max(PresentationTimeSeconds,0),1024);
float2 p=UV*2-1;
float2 uv=.5*(p*(1+clamp(DistortionAmount,0,.03)*dot(p,p))+1);
uv.x+=clamp(DeceptionSyncTear,0,.01)*sin(uv.y*90+t*3)*saturate(DeceptionAmount);
float valid=step(0,uv.x)*step(uv.x,1)*step(0,uv.y)*step(uv.y,1);
float radius=clamp(CornerRadiusPx,0,min(size.x,size.y)*.5);
float2 q=abs((UV-.5)*size)-(size*.5-radius);
float d=length(max(q,0))+min(max(q.x,q.y),0)-radius;
float aa=max(fwidth(d),.5);
float aperture=1-smoothstep(-aa,aa,d);
float softness=clamp(WipeSoftnessPx,.5,8)/size.y;
float wipe=WipeProgress<=0?0:(WipeProgress>=1?1:1-smoothstep(WipeProgress-softness,WipeProgress+softness,UV.y));
float scan=1-clamp(ScanlineStrength,0,.35)*(.5+.5*cos(6.2831853*(UV.y*size.y/clamp(ScanlinePitchPx,1,8)+saturate(DeceptionAmount)*.03*sin(t))));
float noise=frac(sin(dot(floor(UV*size),float2(12.9898,78.233))+floor(t*30))*43758.5453);
float3 color=Texture2DSample(OpticRenderTarget,OpticRenderTargetSampler,uv).rgb*PhosphorTint.rgb*scan;
color=lerp(color,noise*.15,saturate(SignalOfflineNoise));
color+=noise*saturate(ScrubTrackingNoise)*.04+saturate(VoiceBleedIntensity)*.015;
return float4(saturate(color),valid*aperture*wipe);
''')
for prop,mask in [(u.MaterialProperty.MP_EMISSIVE_COLOR,'RGB'),(u.MaterialProperty.MP_OPACITY,'A')]:
    c=node(u.MaterialExpressionComponentMask)
    for ch in 'rgba': c.set_editor_property(ch,ch.upper() in mask)
    link(custom,c,'Input');assert lib.connect_material_property(c,'',prop)
errors=lib.recompile_material(m)
assert not errors, list(errors)
assert u.EditorAssetLibrary.save_loaded_asset(m)
assert m.get_editor_property('material_domain')==u.MaterialDomain.MD_UI
# Read-only inspection of the selected references. No expressions or curve data are copied.
reference_inspection=[]
for reference in ['/Game/Loom/Materials/Functions/Custom_HLSL/MF_Loom_LineMin',
                  '/Game/Loom/Materials/Functions/Custom_HLSL/MF_Loom_RadialSegments_Filled']:
    asset=u.load_asset(reference) if u.EditorAssetLibrary.does_asset_exist(reference) else None
    # Follow input connections backward from function outputs, retaining only graph metadata.
    expressions=lib.get_material_function_expressions(asset) if asset else []
    outputs=[e for e in expressions if e.get_class().get_name()=='MaterialExpressionFunctionOutput']
    visited=set()
    def walk(expression):
        if not expression or expression.get_path_name() in visited:return
        visited.add(expression.get_path_name())
        for source in (lib.get_inputs_for_material_function_expression(asset,expression) or []):walk(source)
    for output in outputs:walk(output)
    reference_inspection.append({'path':reference,'loaded':asset is not None,
                                 'expression_count':len(expressions),'connected_count':len(visited),
                                 'outputs':[str(e.get_editor_property('output_name')) for e in outputs]})
registry=u.AssetRegistryHelpers.get_asset_registry();registry.search_all_assets(True)
options=u.AssetRegistryDependencyOptions(True,True,True,True,True)
violations=[]
assets=registry.get_assets_by_path('/Game',recursive=True)
for asset in assets:
    package=str(asset.package_name)
    if package.startswith('/Game/Loom/'):continue
    for dependency in (registry.get_dependencies(asset.package_name,options) or []):
        if str(dependency).startswith('/Game/Loom'):
            violations.append([package,str(dependency)])
report={'packages_checked':len(assets),'external_loom_dependencies':violations,'material':path,'domain':str(m.get_editor_property('material_domain')),'references':reference_inspection}
(root/'Saved/TerminalAssetAudit.json').write_text(json.dumps(report,indent=2))
assert not violations,violations
u.log('TERMINAL_ASSET_AUDIT_PASS '+json.dumps(report))

