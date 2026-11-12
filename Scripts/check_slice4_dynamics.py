"""Lightweight native fixed-step diagnostics. Run with Unreal Python/NullRHI; no map saves.

This exercises the production force integration and gesture APIs, not viewport/input rendering.
"""
import math
import unreal

unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/AttractionGym")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
board = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.AttractionSimulation))
types = (unreal.AttractionArchetype.DRIVER, unreal.AttractionArchetype.BUFFER, unreal.AttractionArchetype.ANCHOR)
original_rules = list(board.get_editor_property("BondRules"))

def node(kind, x=0, y=0, pinned=False, velocity=0):
    result = unreal.AttractionSimulation.make_archetype(kind)
    result.set_editor_property("Position", unreal.Vector(x,y,0))
    result.set_editor_property("Velocity", unreal.Vector(velocity,0,0))
    result.set_editor_property("bPinned", pinned)
    return result

def configure(nodes, field=800000, viscosity=0, drag=0, rules=None):
    board.set_editor_property("bUseDemo", False)
    board.set_editor_property("InitialNodes", nodes)
    board.set_editor_property("BondRules", [] if rules is None else rules)
    board.set_editor_property("FieldStrength", field)
    board.set_editor_property("Viscosity", viscosity)
    board.set_editor_property("Drag", drag)
    board.set_editor_property("bRunning", True)
    board.reset_simulation()

def states():
    return list(board.get_editor_property("Nodes"))

def step(count=1):
    for _ in range(count):
        board.advance_simulation(1/120)

def value(n, name):
    return n.get_editor_property(name)

# Native wide drop bounds, not a copy of the input projection formula.
configure([])
assert board.drop_node(node(types[0]), unreal.Vector(4000,2400,0)) == 0
assert board.drop_node(node(types[0]), unreal.Vector(4401,0,0)) == -1
assert board.drop_node(node(types[0]), unreal.Vector(0,2601,0)) == -1

configure([node(types[0],-200), node(types[0],200)])
step()
assert value(states()[0],"Velocity").x<0 and value(states()[1],"Velocity").x>0
configure([node(types[0],-200), node(types[2],200)])
step()
a,b = states()
momentum = value(a,"Velocity").x*value(a,"Mass") + value(b,"Velocity").x*value(b,"Mass")
assert value(a,"Velocity").x>0 and value(b,"Velocity").x<0 and momentum>0.1
for kind in types:
    configure([node(types[1]),node(kind,500)])
    step()
    assert value(states()[0],"Velocity").x>0, "Buffer lacks medium-range pull"
configure([node(types[2],-350),node(types[2],350)])
step()
assert abs(value(states()[0],"Velocity").x)<1e-5
configure([node(types[2],-50),node(types[2],50)])
step()
assert value(states()[0],"Velocity").x>0, "Anchor lacks close cohesion"

# Crowding oscillator duration/cooldown with fixed neighbors; no trajectories mocked.
configure([node(types[1],pinned=True),node(types[2],-40,pinned=True),node(types[2],40,pinned=True)])
pulses = []
duration = 0
for _ in range(1440):
    step()
    n = states()[0]
    assert 0 <= value(n,"InternalEnergy") <= 1
    if value(n,"PulseTimeRemaining")>0:
        duration += 1/120
    elif duration:
        pulses.append(duration)
        duration = 0
assert len(pulses)>=2 and all(abs(t-0.35)<0.02 for t in pulses), pulses
configure([node(types[1],pinned=True),node(types[2],-40,pinned=True),node(types[2],0,40,pinned=True)])
for _ in range(360):
    step()
    if value(states()[0],"InternalEnergy")>0.98:
        break
assert value(states()[0],"InternalEnergy")>0.98
assert board.drop_node(node(types[2]),unreal.Vector(200,0,0))==3
step(4)
assert value(states()[0],"PulseTimeRemaining")>0 and value(states()[3],"Velocity").x>0

configure([node(types[0])])
assert board.toggle_nearest_pin(unreal.Vector(100,0,0))
board.stir(unreal.Vector(-100,0,0),unreal.Vector(100,0,0))
assert value(states()[0],"bPinned") and value(states()[0],"Velocity").length()==0
assert board.toggle_nearest_pin(unreal.Vector(100,0,0))
board.stir(unreal.Vector(-100,0,0),unreal.Vector(100,0,0))
assert value(states()[0],"Velocity").x>0
assert not board.toggle_nearest_pin(unreal.Vector(2000,2000,0))

configure([node(types[0],4390,velocity=400)],field=0)
step(1200)
assert abs(value(states()[0],"Position").x)<4400 and value(states()[0],"Velocity").x<0
configure([node(types[0],-70),node(types[2],70)],rules=original_rules)
step()
tethers = board.get_editor_property("Tethers")
assert len(tethers)==1
assert tethers[0].get_editor_property("Rule").get_editor_property("BreakStrain")>=1.7999
assert unreal.AttractionSimulation.make_archetype(types[2]).get_editor_property("Valence")==4

# Mixed seeded demo: inspect finite state, pinned stability and actual pulse events over 20 s.
board.set_editor_property("bUseDemo",True)
board.set_editor_property("FieldStrength",800000)
board.set_editor_property("Viscosity",2)
board.set_editor_property("Drag",0.35)
board.set_editor_property("BondRules",original_rules)
board.reset_simulation()
initial = states()
assert len(initial)==9 and all(sum(value(n,"Archetype")==kind for n in initial)==3 for kind in types)
pin_position = value(initial[2],"Position")
saw_pulse = False
for frame in range(2400):
    step()
    if frame%8==0:
        for n in states():
            p,v = value(n,"Position"),value(n,"Velocity")
            assert all(math.isfinite(x) for x in (p.x,p.y,v.x,v.y))
            assert abs(p.x)<5000 and abs(p.y)<3200 and v.length()<=1001
            saw_pulse |= value(n,"PulseTimeRemaining")>0
assert saw_pulse and value(states()[2],"Position")==pin_position
end_speed = max(value(n,"Velocity").length() for n in states())
unreal.log("SLICE4_DEMO_20S: maximum final speed="+str(end_speed))
assert end_speed>1, "Seeded demo became static"
board.reset_simulation()
assert len(states())==9 and value(states()[0],"Position")==value(initial[0],"Position")
unreal.log("SLICE4_DYNAMICS_OK: bounds, nonreciprocal archetypes, pulse durations="+str(pulses)+
           ", outward pulse, pin/stir, soft containment, driver tethers, 20s finite mixed demo, reset")
