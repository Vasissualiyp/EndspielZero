# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate all 17 M24 meshes. Python 3 standard library only."""
import math
from mesh import Mesh, add, frame, tilt


def projector():
    m=Mesh()
    m.lathe('weighted_base',[(0,0),(.117,0),(.12,.003),(.12,.021),(.117,.025),(0,.025)],64)
    m.tube('recessed_collar',.105,.083,.024,.061,64)
    m.tube('lens_bezel',.115,.084,.058,.074,64)
    for i in range(6):
        a=i*math.pi/3
        m.cylinder('bezel_fastener_'+str(i),(.111*math.cos(a),.111*math.sin(a),.072),(.111*math.cos(a),.111*math.sin(a),.074),.0028,12,.0005)
    m.write('projector_body')
    m=Mesh()
    m.cylinder('flush_frosted_disc',(0,0,.071),(0,0,.075),.084,64,.001)
    centers=[(0,0)]+[(.047*math.cos(i*math.pi/3),.047*math.sin(i*math.pi/3)) for i in range(6)]
    for i,(x,y) in enumerate(centers):
        m.lathe('lens_'+str(i),[(0,.064),(.017,.064),(.019,.066),(.019,.068),(.015,.070),(0,.0705)],24,lambda p,x=x,y=y:add(p,(x,y,0)))
    m.write('projector_lens')
    m=Mesh()
    for i in range(6):
        a=i*math.pi/3
        def tr(p,a=a): return (p[0]*math.cos(a)-p[1]*math.sin(a),p[0]*math.sin(a)+p[1]*math.cos(a),p[2])
        m.box('status_pill_'+str(i),(0,-.105,.044),(.013,.003,.004),.0014,tr)
    m.write('projector_leds')


def lamp():
    m=Mesh()
    m.cylinder('weighted_base',(0,0,0),(0,0,.025),.08,48,.003)
    a=(0,0,.04); b=(0,.055,.305); c=(0,-.303,.527)
    m.beam('lower_arm',a,b,.018,.026)
    m.beam('upper_arm',b,c,.018,.026)
    for i,p in enumerate((a,b,c)):
        m.cylinder('pivot_'+str(i),add(p,(-.023,0,0)),add(p,(.023,0,0)),.019,24,.002)
        m.cylinder('pivot_cap_'+str(i),add(p,(-.026,0,0)),add(p,(-.023,0,0)),.011,20,.001)
    # Counter-spring alongside lower arm, with hooks into the pivot brackets.
    p0=(-.027,.009,.078); p1=(-.027,.045,.257); tr=frame(p0,p1)
    points=[tr((.008*math.cos(t*2*math.pi*11),.008*math.sin(t*2*math.pi*11),t*.182)) for t in [i/176 for i in range(177)]]
    m.wire('counter_spring',points,.0017,6)
    m.beam('spring_lower_anchor',(-.03,.009,.078),(-.008,.009,.078),.006,.008)
    m.beam('spring_upper_anchor',(-.03,.045,.257),(-.008,.045,.257),.006,.008)
    head=tilt((0,-.320,.528),math.radians(20))
    m.box('led_head',(0,0,0),(.18,.05,.014),.003,head)
    # Bezel rails leave the underside emitter exposed.
    m.box('head_front_lip',(0,-.023,-.008),(.176,.004,.006),.001,head)
    m.box('head_back_lip',(0,.023,-.008),(.176,.004,.006),.001,head)
    m.write('lamp')
    e=Mesh(); e.box('emitter',(0,0,-.008),(.164,.036,.002),.0005,head); e.write('lamp_head_emitter')


def magazine():
    m=Mesh()
    m.box('back_panel',(-.006,0,.53),(.012,.964,1.02))
    for y in (-.48,.48):
        m.box('upright_'+str(y),(-.125,y,.525),(.25,.04,1.05))
        # A second raised rib makes these read as extrusion profiles.
        m.box('upright_rib_'+str(y),(-.247,y,.525),(.006,.014,1.018),.002)
    for z in (.025,1.025):
        m.box('cross_member_'+str(z),(-.124,0,z),(.248,.92,.05))
    for i in range(8):
        y=(i-3.5)*.11
        for side in (-1,1):
            m.box(f'bay_{i}_guide_{side}',(-.131,y+side*.021,.525),(.226,.012,.95),.002)
        m.box('led_mount_'+str(i),(-.247,y,.055),(.006,.028,.032),.0015)
    for i,y in enumerate((-.34,-.12,.12,.34)):
        m.cylinder('cable_gland_'+str(i),(-.06,y,1.045),(-.06,y,1.085),.017,16,.002)
        m.cylinder('gland_nut_'+str(i),(-.06,y,1.05),(-.06,y,1.067),.021,6,.002)
        m.cylinder('gland_boot_'+str(i),(-.06,y,1.083),(-.06,y,1.10),.009,16,.002)
    m.write('magazine')
    m=Mesh()
    for i in range(8): m.box('slot_led_'+str(i),(-.2505,(i-3.5)*.11,.055),(.003,.018,.009),.001)
    m.write('magazine_leds')


def carriage():
    m=Mesh()
    # Four plate quarters leave a real through-hole under the motor.
    for x in (-.09,.09): m.box('plate_side_'+str(x),(x,0,.013),(.12,.30,.026))
    for y in (-.09,.09): m.box('plate_end_'+str(y),(0,y,.013),(.06,.12,.026))
    m.tube('mast_socket',.043,.026,0,.029,40)
    for x in (-.112,.112):
        for y in (-.112,.112):
            m.box(f'bearing_block_{x}_{y}',(x,y,.047),(.062,.068,.042))
            m.cylinder(f'rail_wheel_{x}_{y}',(x-.034,y,.056),(x+.034,y,.056),.028,24,.002)
    m.box('motor_housing',(0,0,.064),(.108,.10,.052),.004)
    for y in (-.033,-.011,.011,.033): m.box('motor_cooling_rib_'+str(y),(0,y,.089),(.09,.008,.002),.0007)
    for i in range(4):
        m.box('drag_chain_link_'+str(i),(0,.069+i*.021,.047),(.055,.017,.027),.002)
    m.write('gantry_carriage')


def masts():
    m=Mesh(); m.tube('outer_tube',.023,.0197,-.575,0,48,.001)
    m.tube('lower_collar',.025,.0197,-.60,-.567,48,.0015)
    m.write('gantry_mast_outer')
    m=Mesh(); m.tube('inner_tube',.019,.015,-.573,0,48,.001)
    # An annular shoulder bridges the tube to the larger socket. The underside
    # remains open at Ø71 mm for the slab's Ø70 mm connector boss.
    m.lathe('clamp_ring',[(.0375,-.582),(.043,-.582),(.045,-.580),
                         (.045,-.567),(.043,-.565),(.017,-.565),
                         (.015,-.567),(.015,-.571),(.017,-.573),
                         (.0335,-.573),(.0355,-.575),(.0355,-.580)],48)
    for i in range(4):
        a=i*math.pi/2
        def tr(p,a=a): return (p[0]*math.cos(a)-p[1]*math.sin(a),p[0]*math.sin(a)+p[1]*math.cos(a),p[2])
        m.box('grip_prong_'+str(i),(.040,0,-.584),(.010,.015,.032),.002,tr)
        m.box('grip_lip_'+str(i),(.036,0,-.597),(.010,.015,.006),.0015,tr)
    m.write('gantry_mast_inner')


def slab():
    m=Mesh()
    # A hollow tray with an open top. Its interior floor is below the LED plane.
    def loop(h,z,b=.003): return [(-h+b,-h,z),(h-b,-h,z),(h,-h+b,z),(h,h-b,z),(h-b,h,z),(-h+b,h,z),(-h,h-b,z),(-h,-h+b,z)]
    rings=[loop(.217,-.014),loop(.220,-.011),loop(.220,-.002),loop(.218,0),loop(.211,0),loop(.209,-.002),loop(.209,-.008)]
    polys=[list(reversed(rings[0])),rings[-1]]
    for a,b in zip(rings,rings[1:]):
        for i in range(8): polys.append([a[i],a[(i+1)%8],b[(i+1)%8],b[i]])
    m.shell('open_top_tray',polys)
    for i in range(19):
        x=(i-9)*.021
        if abs(x)<.047:
            for y in (-.128,.128): m.box(f'heatsink_{i}_{y}',(x,y,-.018),(.007,.14,.008),.002)
        else: m.box('heatsink_'+str(i),(x,0,-.018),(.007,.396,.008),.002)
    m.cylinder('connector_boss',(0,0,-.025),(0,0,-.013),.035,48,.002)
    m.write('slab_back')


def monitor():
    m=Mesh()
    m.box('foot',(0,0,.008),(.23,.18,.016),.003)
    m.box('stand',(0,.025,.065),(.042,.042,.104),.003)
    m.box('rear_housing',(0,.026,.235),(.488,.034,.242),.004)
    # Recessed dark screen, four separate bevelled bezel rails.
    m.box('off_screen',(0,.007,.235),(.464,.006,.214),.002)
    for x in (-.241,.241): m.box('side_bezel_'+str(x),(x,.003,.235),(.018,.022,.25),.003)
    for z in (.119,.351): m.box('horizontal_bezel_'+str(z),(0,.003,z),(.464,.022,.018),.003)
    for x in (-.12,-.06,0,.06,.12): m.box('rear_vent_'+str(x),(x,.045,.278),(.026,.004,.046),.001)
    m.write('bench_monitor')


def clutter():
    m=Mesh()
    m.lathe('cup',[(0,0),(.035,0),(.039,.004),(.040,.101),(.039,.105),(.036,.105),(.035,.101),(.034,.008),(0,.008)],48)
    points=[(.038+.030*math.sin(math.pi*t),0,.079-.055*t) for t in [i/24 for i in range(25)]]
    m.wire('handle',points,.006,12); m.write('clutter_mug')
    m=Mesh()
    m.box('case',(0,0,.044),(.24,.13,.088),.004)
    m.box('lid',(0,0,.097),(.24,.13,.022),.003)
    for x in (-.075,.075):
        m.box('latch_'+str(x),(x,-.066,.080),(.022,.008,.032),.002)
        m.cylinder('hinge_'+str(x),(x-.018,.064,.087),(x+.018,.064,.087),.006,16,.001)
    for x in (-.043,.043): m.box('handle_post_'+str(x),(x,0,.118),(.012,.016,.028),.002)
    m.box('carry_handle',(0,0,.131),(.098,.016,.014),.003)
    m.write('clutter_toolbox')
    m=Mesh()
    m.tube('hub',.030,.012,0,.09,40)
    for z in (.0,.080): m.tube('flange_'+str(z),.065,.012,z,z+.010,48,.002)
    profile=[(.030,.012),(.048,.012)]
    for i in range(11):
        z=.014+i*.006
        profile.extend([(.052,z),(.053,z+.002),(.051,z+.004)])
    profile.extend([(.048,.078),(.030,.078)])
    m.lathe('wound_cable',profile,48)
    m.write('clutter_spool')
    m=Mesh()
    m.box('rubber_case',(0,0,.017),(.084,.155,.034),.004)
    m.box('inset_display',(0,.041,.035),(.060,.044,.004),.0015)
    m.cylinder('selector',(0,-.014,.033),(0,-.014,.043),.020,24,.002)
    m.box('selector_grip',(0,-.014,.045),(.008,.029,.006),.002)
    for x in (-.023,0,.023): m.tube('jack_'+str(x),.005,.0025,.033,.037,16,.0007,lambda p,x=x:add(p,(x,-.058,0)))
    for x in (-.055,.055):
        m.cylinder('probe_'+str(x),(x,-.075,.011),(x,.036,.011),.005,16,.001)
        m.cylinder('probe_tip_'+str(x),(x,.035,.011),(x,.057,.011),.0017,12,.0004)
        # Return cable from probe tail to bottom socket, with a smooth U bend.
        points=[(x*(1-t)+x*.4*t, -.075-.032*math.sin(t*math.pi), .011+.022*t) for t in [i/20 for i in range(21)]]
        m.wire('test_lead_'+str(x),points,.002,8)
    m.write('clutter_multimeter')


def door():
    m=Mesh()
    # Backing/seal closes the gaps behind the articulated face slats. It stays
    # behind their visible ribs and within the existing 80 mm depth envelope.
    m.box('continuous_back_seal',(0,-.024,1.2),(2.94,.012,2.36),.003)
    for i in range(23):
        z=.1+i*.1
        m.box('slat_'+str(i),(0,0,z+.047), (2.91,.044,.094),.003)
        m.box('horizontal_rib_'+str(i),(0,.027,z+.016),(2.89,.014,.012),.003)
    m.box('bottom_rail',(0,.004,.05),(2.94,.060,.1),.003)
    for x in (-1.475,1.475): m.box('side_guide_'+str(x),(x,0,1.2),(.05,.08,2.4),.003)
    m.box('top_rail',(0,0,2.375),(2.90,.06,.05),.003)
    # Recessed lifting grip sits on the +Y room side.
    m.box('grip_top',(0,.033,.22),(.18,.014,.014),.003)
    for x in (-.083,.083): m.box('grip_end_'+str(x),(x,.032,.203),(.014,.016,.026),.003)
    m.write('door_roller')

if __name__=='__main__':
    for generate in (projector,lamp,magazine,carriage,masts,slab,monitor,clutter,door): generate()
