"""Slice 5 native topology/authorization regression checks. No maps saved."""
import math
import unreal
unreal.EditorLoadingAndSavingUtils.load_map('/Game/Maps/AttractionGym')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
b=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.AttractionSimulation))
def get(name):return b.get_editor_property(name)
def nodes():return list(get('nodes'))
def advance(seconds):
    for _ in range(round(seconds*120)):b.advance_simulation(1/120)
def signature():return (len(nodes()),len(get('tethers')),sum(n.get_editor_property('agent_role')!=unreal.AttractionAgentRole.NONE for n in nodes()))
b.set_editor_property('bUseDemo',True);b.reset_simulation()
assert len(get('stages'))==3
assert b.connected_agents()==3
assert all(n.get_editor_property('bConnectedToRoot') for n in nodes())
assert not get('bRewardActive')
initial=signature();advance(5);assert signature()==initial,'Dormant agents changed topology'
assert all(0<=b.sample_target_height(unreal.Vector2D(x,y))<=1 for x in range(-4400,4401,200) for y in range(-2600,2601,200))
b.reset_simulation();origin=nodes()[1].get_editor_property('position')
b.set_reward(unreal.Vector2D(origin.x+500,origin.y),True)
advance(12)
assert len(nodes())>4,'Authorized builder failed to extrude'
assert all(math.isfinite(n.get_editor_property('position').x) and math.isfinite(n.get_editor_property('velocity').length()) for n in nodes())
b.set_reward(get('reward_center'),False)
# Physics can break strained edges; OFF specifically prohibits new topology or dissolution.
count=len(nodes());advance(5);assert len(nodes())==count,'OFF changed node count'
b.reset_simulation();b.set_reward(unreal.Vector2D(4200,2400),True)
advance(6);assert len(nodes())==4,'Remote disc authorized work'
# Cut both root links to the Builder, preserving detached internal topology.
b.reset_simulation()
while True:
    tethers=list(get('tethers')); links=[i for i,t in enumerate(tethers) if t.get_editor_property('a')==0 or t.get_editor_property('b')==0]
    if not links:break
    b.slice_tether(links[-1])
assert b.connected_agents()==0
assert len(get('tethers'))==2,'Cut destroyed internal patch tethers'
b.set_reward(unreal.Vector2D(-1000,0),True);advance(8)
assert len(nodes())==4,'Severed agent performed work'
b.graft_patch(2);assert b.connected_agents()==3,'Graft failed to restore cohesive patch'
# Detach again before carryover audit.
while True:
    links=[i for i,t in enumerate(get('tethers')) if t.get_editor_property('a')==0 or t.get_editor_property('b')==0]
    if not links:break
    b.slice_tether(links[-1])
b.next_stage();assert get('stage_index')==1 and b.connected_agents()==0,'Detached agents carried over'
b.reset_simulation();assert b.connected_agents()==3
b.next_stage();assert get('stage_index')==2 and b.connected_agents()==3
assert all(n.get_editor_property('potency_tier')==2 for n in nodes() if n.get_editor_property('agent_role')!=unreal.AttractionAgentRole.NONE)
b.next_stage();assert get('stage_index')==0
assert all(n.get_editor_property('potency_tier')==3 for n in nodes() if n.get_editor_property('agent_role')!=unreal.AttractionAgentRole.NONE)
b.reset_simulation();assert all(n.get_editor_property('potency_tier')==1 for n in nodes())
assert 0<=b.target_coverage()<=100
# Focused role fixtures use production grafting to create an intentional open span.
def configure_fixture(role):
    initial=[]
    for i,(x,y) in enumerate(((-900,0),(-660,0),(-450,180),(-450,-180))):
        n=unreal.AttractionNode()
        n.set_editor_property('Position',unreal.Vector(x,y,0))
        n.set_editor_property('bRootAnchor',i==0)
        if i==1:n.set_editor_property('AgentRole',role)
        initial.append(n)
    b.set_editor_property('bUseDemo',False)
    b.set_editor_property('InitialNodes',initial)
    b.set_editor_property('MaxAttachDist',300)
    b.reset_simulation()
    for i in (1,2,3):b.graft_patch(i)
    b.set_reward(unreal.Vector2D(-660,0),True)
configure_fixture(unreal.AttractionAgentRole.FILLER)
start_edges=len(get('tethers'));advance(5)
assert len(get('tethers'))>start_edges,'Filler failed to brace an open interior span'
configure_fixture(unreal.AttractionAgentRole.TRIMMER)
advance(8)
assert len(nodes())<4,'Trimmer failed to creep into cleared interior'
# Low-field saddles stall fillers, even while authorized and root connected.
initial=[]
for i,x in enumerate((3700,3940)):
    n=unreal.AttractionNode();n.set_editor_property('Position',unreal.Vector(x,0,0));n.set_editor_property('bRootAnchor',i==0)
    if i==1:n.set_editor_property('AgentRole',unreal.AttractionAgentRole.FILLER)
    initial.append(n)
b.set_editor_property('InitialNodes',initial);b.reset_simulation();b.graft_patch(1)
b.set_reward(unreal.Vector2D(3900,0),True);before=signature();advance(5)
assert signature()==before,'Filler crossed low-height region'
unreal.log('SLICE5_NATIVE_OK: OFF/remote/severed gates, authorized extrusion, finite physics, cohesive cuts, three stages, survivor potency/reset; no viewport claims')
