"""Build the smart-picture-frame enclosure as a parametric Fusion 360 assembly.

Reads dimensions.json (same source as build_model.py) and creates one new design:

    root
    +-- Picture frame case   (tilted 10 deg, seated in the stand)
    |   +-- Front shell
    |   +-- Rear cover
    |   +-- Kit envelope (reference)   85 x 112 x 16 mm block, semi-transparent
    +-- Desk stand
    +-- Alignment gauge      (lying flat beside the stand)

Run through FusionBridge:
    py -3.14 fusion_client.py run "smart picture frame\\fusion_design.py" --timeout 600
or from Fusion's Scripts and Add-Ins dialog.  Exports an .f3d archive and a STEP file
to the fusion\\ sub-folder and returns bounding boxes, volumes and interference checks.

Coordinate convention: the case is modelled in a right-handed frame with X to the
viewer's right, Y up and Z toward the viewer (front face at Z = case_depth, back at 0).
build_model.py's "z into the case" values are converted with zf = case_depth - z.
"""

import json
import math
import os
import traceback

import adsk.core
import adsk.fusion

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, 'fusion')
EPS = 0.02

NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation


def mm(v):
    return v / 10.0


def V(v_mm):
    return adsk.core.ValueInput.createByReal(mm(v_mm))


def P(x, y, z=0.0):
    return adsk.core.Point3D.create(mm(x), mm(y), mm(z))


def load_params():
    with open(os.path.join(HERE, 'dimensions.json'), encoding='utf-8') as f:
        p = json.load(f)
    d = {}
    d['w'] = p['wall'] + p['side_clearance'] + p['kit_width'] + p['side_clearance'] + p['wall']
    d['h'] = p['wall'] + p['bottom_band'] + p['kit_height'] + p['top_band'] + p['wall']
    d['bx'] = p['wall'] + p['side_clearance']
    d['by'] = p['wall'] + p['bottom_band']
    d['depth'] = p['shell_depth']
    d['D'] = p['shell_depth'] + p['lid_thickness']
    d['board_back'] = p['front_thickness'] + p['screen_recess'] + p['kit_depth']
    d['pad_top'] = d['board_back'] + p['rear_pad_thickness']
    d['wx'] = d['bx'] + p['screen_center_from_kit_left'] - p['window_width'] / 2
    d['wy'] = d['by'] + p['screen_center_from_kit_bottom'] - p['window_height'] / 2
    r = p['screw_boss_diameter'] / 2
    wall = p['wall']
    usb_x0 = d['bx'] + p['usb_bay_from_kit_left']
    usb_x1 = usb_x0 + p['usb_bay_width']
    d['usb_x0'] = usb_x0
    d['screws'] = [(usb_x1 + r + 1, wall + r - 1),
                   (d['w'] - wall - r + 1, wall + r - 1),
                   (wall + r - 1, d['h'] - wall - r + 1),
                   (d['w'] - wall - r + 1, d['h'] - wall - r + 1)]
    d['sw'] = d['w'] + 2 * p['stand_margin']
    d['slot_w'] = d['w'] + p['stand_clearance']
    d['slot_d'] = d['D'] + p['stand_clearance']
    d['slot_y'] = 18.0
    d['slot_z'] = 6 + math.sin(math.radians(p['stand_angle'])) * d['slot_d']
    return p, d


# ---------------------------------------------------------------- sketch helpers

def rrect(sk, x, y, w, h, r):
    """Rounded rectangle (lower-left corner x,y) on a sketch parallel to XY."""
    S = sk.modelToSketchSpace
    if r <= 0:
        sk.sketchCurves.sketchLines.addTwoPointRectangle(S(P(x, y)), S(P(x + w, y + h)))
        return
    lines, arcs = sk.sketchCurves.sketchLines, sk.sketchCurves.sketchArcs
    q = math.pi / 2
    lines.addByTwoPoints(S(P(x + r, y)), S(P(x + w - r, y)))
    arcs.addByCenterStartSweep(S(P(x + w - r, y + r)), S(P(x + w - r, y)), q)
    lines.addByTwoPoints(S(P(x + w, y + r)), S(P(x + w, y + h - r)))
    arcs.addByCenterStartSweep(S(P(x + w - r, y + h - r)), S(P(x + w, y + h - r)), q)
    lines.addByTwoPoints(S(P(x + w - r, y + h)), S(P(x + r, y + h)))
    arcs.addByCenterStartSweep(S(P(x + r, y + h - r)), S(P(x + r, y + h)), q)
    lines.addByTwoPoints(S(P(x, y + h - r)), S(P(x, y + r)))
    arcs.addByCenterStartSweep(S(P(x + r, y + r)), S(P(x, y + r)), q)


def circle(sk, x, y, dia):
    sk.sketchCurves.sketchCircles.addByCenterRadius(sk.modelToSketchSpace(P(x, y)), mm(dia / 2))


def prism(comp, name, draw, z0, z1, op, target=None, pick='all', expr=None):
    """Sketch on the component XY plane, extrude from z0 to z1 (mm, z1 > z0)."""
    sk = comp.sketches.add(comp.xYConstructionPlane)
    sk.name = name
    sk.isComputeDeferred = True
    draw(sk)
    sk.isComputeDeferred = False
    profiles = adsk.core.ObjectCollection.create()
    if pick == 'all':
        for pr in sk.profiles:
            profiles.add(pr)
    elif pick == 'largest':
        profiles.add(max(sk.profiles, key=lambda pr: pr.areaProperties().area))
    elif pick == 'ring':
        profiles.add(next(pr for pr in sk.profiles if pr.profileLoops.count == 2))
    if profiles.count == 0:
        raise RuntimeError(f'{name}: sketch produced no profiles')
    ext = comp.features.extrudeFeatures
    inp = ext.createInput(profiles, op)
    if abs(z0) > 1e-9:
        inp.startExtent = adsk.fusion.OffsetStartDefinition.create(V(z0))
    dist = adsk.core.ValueInput.createByString(expr) if expr else V(z1 - z0)
    inp.setOneSideExtent(adsk.fusion.DistanceExtentDefinition.create(dist),
                         adsk.fusion.ExtentDirections.PositiveExtentDirection)
    if target is not None:
        inp.participantBodies = [target]
    feat = ext.add(inp)
    feat.name = name
    return feat


def placement(origin, x_axis, y_axis, z_axis):
    m = adsk.core.Matrix3D.create()
    m.setWithCoordinateSystem(P(*origin),
                              adsk.core.Vector3D.create(*x_axis),
                              adsk.core.Vector3D.create(*y_axis),
                              adsk.core.Vector3D.create(*z_axis))
    return m


def metrics(body):
    bb = body.boundingBox
    return {'size_mm': [round((bb.maxPoint.x - bb.minPoint.x) * 10, 3),
                        round((bb.maxPoint.y - bb.minPoint.y) * 10, 3),
                        round((bb.maxPoint.z - bb.minPoint.z) * 10, 3)],
            'volume_cm3': round(body.volume, 3),
            'faces': body.faces.count}


# ---------------------------------------------------------------- parts

def build_front_shell(comp, p, d):
    w, h, D = d['w'], d['h'], d['D']
    wall, face, depth, r = p['wall'], p['front_thickness'], d['depth'], p['corner_radius']
    ww, wh, wr = p['window_width'], p['window_height'], p['window_radius']

    def zf(a, b):  # model z range (into the case) -> fusion z range (toward the viewer)
        return D - b, D - a

    body = prism(comp, 'Shell outline', lambda sk: rrect(sk, 0, 0, w, h, r),
                 *zf(0, depth), NEW, expr='shell_depth').bodies.item(0)
    body.name = 'Front shell'

    def cavity(sk):
        rrect(sk, wall, wall, w - 2 * wall, h - 2 * wall, r - wall)
        for x, y in d['screws']:  # bosses stay as material, embedded 1 mm in the walls
            circle(sk, x, y, p['screw_boss_diameter'])
    prism(comp, 'Cavity (leaves screw bosses)', cavity, *zf(face, depth + EPS), CUT, body, pick='largest')
    prism(comp, 'Screen window', lambda sk: rrect(sk, d['wx'], d['wy'], ww, wh, wr),
          *zf(-EPS, face + EPS), CUT, body)
    prism(comp, 'Window inside relief', lambda sk: rrect(sk, d['wx'] - 1, d['wy'] - 1, ww + 2, wh + 2, wr),
          *zf(face - 0.8, face), CUT, body)
    prism(comp, 'Screw pilot holes',
          lambda sk: [circle(sk, x, y, p['screw_pilot_diameter']) for x, y in d['screws']],
          *zf(depth - 12, depth + EPS), CUT, body)
    return body


def build_rear_cover(comp, p, d):
    w, h, D = d['w'], d['h'], d['D']
    wall, depth, lid, r = p['wall'], d['depth'], p['lid_thickness'], p['corner_radius']
    gap = p['lid_clearance']
    bx, by, kw, kh = d['bx'], d['by'], p['kit_width'], p['kit_height']

    def zf(a, b):
        return D - b, D - a

    body = prism(comp, 'Cover plate', lambda sk: rrect(sk, 0, 0, w, h, r),
                 *zf(depth, depth + lid), NEW, expr='lid_thickness').bodies.item(0)
    body.name = 'Rear cover'

    tx = ty = wall + gap
    tw, th = w - 2 * tx, h - 2 * ty

    def tongue(sk):
        rrect(sk, tx, ty, tw, th, r - tx)
        rrect(sk, tx + 1.6, ty + 1.6, tw - 3.2, th - 3.2, r - tx - 1.6)
    prism(comp, 'Locating tongue', tongue, *zf(depth - 2, depth + EPS), JOIN, body, pick='ring')
    prism(comp, 'Tongue clearance at bosses',
          lambda sk: [circle(sk, x, y, p['screw_boss_diameter'] + 0.8) for x, y in d['screws']],
          *zf(depth - 2 - EPS, depth), CUT, body)

    pads = [(bx + 4, by + 4), (bx + kw - 4, by + 4), (bx + 4, by + kh - 4), (bx + kw - 4, by + kh - 4)]
    prism(comp, 'Rear support pads',
          lambda sk: [rrect(sk, x - 3, y - 3, 6, 6, 1) for x, y in pads],
          *zf(d['pad_top'], depth + EPS), JOIN, body)

    bay = (d['usb_x0'] + 1, wall + 1, p['usb_bay_width'] - 2, p['bottom_band'] - 2)
    prism(comp, 'USB cable bay', lambda sk: rrect(sk, *bay, 3), *zf(depth - 3, depth + 4), CUT, body)

    def vents(sk):
        for y in [by + 34, by + kh - 56]:
            x = bx + 10
            while x + 3.2 <= bx + kw - 10:
                rrect(sk, x, y, 3.2, 22, 1.5)
                x += 8
    prism(comp, 'Vent slots', vents, *zf(depth - 3, depth + 4), CUT, body)
    prism(comp, 'Screw clearance holes',
          lambda sk: [circle(sk, x, y, p['screw_clearance_diameter']) for x, y in d['screws']],
          *zf(depth - 3, depth + 4), CUT, body)
    prism(comp, 'Screw head recesses',
          lambda sk: [circle(sk, x, y, 6.4) for x, y in d['screws']],
          *zf(depth + lid - 1.2, depth + 4), CUT, body)
    return body


def build_kit_envelope(comp, p, d):
    D = d['D']
    z0 = p['front_thickness'] + p['screen_recess']
    body = prism(comp, 'Kit envelope 85 x 112 x 16',
                 lambda sk: rrect(sk, d['bx'], d['by'], p['kit_width'], p['kit_height'], 0),
                 D - (z0 + p['kit_depth']), D - z0, NEW, expr='kit_depth').bodies.item(0)
    body.name = 'Kit envelope (reference)'
    try:
        body.opacity = 0.35
    except Exception:
        pass
    return body


def build_stand(comp, p, d):
    sw, sd, sh = d['sw'], p['stand_depth'], p['stand_height']
    a = math.radians(p['stand_angle'])
    body = prism(comp, 'Stand block', lambda sk: rrect(sk, -sw / 2, 0, sw, sd, 6),
                 0, sh, NEW, expr='stand_height').bodies.item(0)
    body.name = 'Desk stand'

    # Tilted slot: rectangle in the YZ side view, rotated by the stand angle, cut
    # symmetrically about the X=0 centre plane.
    sk = comp.sketches.add(comp.yZConstructionPlane)
    sk.name = 'Tilted slot profile'
    oy, oz = d['slot_y'], d['slot_z']
    uy, uz = math.cos(a), -math.sin(a)     # along the slot floor, front -> back
    vy, vz = math.sin(a), math.cos(a)      # up the slot
    L = d['slot_d']
    pts = [(oy, oz), (oy + L * uy, oz + L * uz),
           (oy + L * uy + 60 * vy, oz + L * uz + 60 * vz), (oy + 60 * vy, oz + 60 * vz)]
    S = sk.modelToSketchSpace
    for i in range(4):
        y0, z0 = pts[i]
        y1, z1 = pts[(i + 1) % 4]
        sk.sketchCurves.sketchLines.addByTwoPoints(S(P(0, y0, z0)), S(P(0, y1, z1)))
    inp = comp.features.extrudeFeatures.createInput(sk.profiles.item(0), CUT)
    inp.setSymmetricExtent(V(d['slot_w']), True)
    inp.participantBodies = [body]
    comp.features.extrudeFeatures.add(inp).name = 'Tilted slot'

    notch_x = -d['slot_w'] / 2 + d['usb_x0'] - 1
    prism(comp, 'Cable notch',
          lambda sk: rrect(sk, notch_x, d['slot_y'] + L * 0.55, p['usb_bay_width'] + 2,
                           sd - (d['slot_y'] + L * 0.55) + 2, 0),
          6, sh + 1, CUT, body)
    return body


def build_gauge(comp, p):
    kw, kh = p['kit_width'], p['kit_height']
    ww, wh = p['window_width'], p['window_height']
    body = prism(comp, 'Gauge plate', lambda sk: rrect(sk, 0, 0, kw, kh, 2), 0, 0.8, NEW).bodies.item(0)
    body.name = 'Alignment gauge'
    prism(comp, 'Gauge window',
          lambda sk: rrect(sk, p['screen_center_from_kit_left'] - ww / 2,
                           p['screen_center_from_kit_bottom'] - wh / 2, ww, wh, p['window_radius']),
          -EPS, 0.8 + EPS, CUT, body)
    return body


# ---------------------------------------------------------------- main

def run(context):
    app = adsk.core.Application.get()
    ui = app.userInterface
    try:
        p, d = load_params()
        doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent

        # User parameters: the driving numbers, visible in Modify > Change Parameters.
        up = design.userParameters
        for name, val, unit, note in [
            ('kit_width', p['kit_width'], 'mm', 'Freenove FNK0086 assembled kit (manufacturer)'),
            ('kit_height', p['kit_height'], 'mm', 'Freenove FNK0086 assembled kit (manufacturer)'),
            ('kit_depth', p['kit_depth'], 'mm', 'Freenove FNK0086 assembled kit (manufacturer)'),
            ('wall', p['wall'], 'mm', 'Side wall thickness'),
            ('front_thickness', p['front_thickness'], 'mm', 'Front face thickness'),
            ('shell_depth', p['shell_depth'], 'mm', 'Front shell depth (without cover)'),
            ('lid_thickness', p['lid_thickness'], 'mm', 'Rear cover plate thickness'),
            ('side_clearance', p['side_clearance'], 'mm', 'Gap between kit and side walls'),
            ('bottom_band', p['bottom_band'], 'mm', 'Band under the kit: USB plug + 2 screws'),
            ('top_band', p['top_band'], 'mm', 'Band above the kit: 2 screws'),
            ('window_width', p['window_width'], 'mm', 'Screen window'),
            ('window_height', p['window_height'], 'mm', 'Screen window'),
            ('screen_center_x', p['screen_center_from_kit_left'], 'mm', 'Active screen centre from kit left (measured)'),
            ('screen_center_y', p['screen_center_from_kit_bottom'], 'mm', 'Active screen centre from kit bottom (measured)'),
            ('stand_angle', p['stand_angle'], 'deg', 'Desk stand lean'),
            ('stand_depth', p['stand_depth'], 'mm', 'Desk stand front-to-back'),
            ('stand_height', p['stand_height'], 'mm', 'Desk stand height'),
            ('case_width', d['w'], 'mm', 'Derived outer width'),
            ('case_height', d['h'], 'mm', 'Derived outer height'),
            ('case_depth', d['D'], 'mm', 'Derived outer depth'),
        ]:
            up.add(name, adsk.core.ValueInput.createByReal(val if unit == 'deg' else mm(val)), unit, note)

        # --- case (shell + cover + kit reference), seated in the stand at stand_angle
        a = math.radians(p['stand_angle'])
        seat = p['stand_clearance'] / 2 + d['D']      # slot-floor distance from the front wall to the case back
        case_origin = (-d['w'] / 2,
                       d['slot_y'] + seat * math.cos(a),
                       d['slot_z'] - seat * math.sin(a))
        case_matrix = placement(case_origin, (1, 0, 0),
                                (0, math.sin(a), math.cos(a)),
                                (0, -math.cos(a), math.sin(a)))
        case_occ = root.occurrences.addNewComponent(case_matrix)
        case_occ.component.name = 'Picture frame case'
        identity = adsk.core.Matrix3D.create()

        shell_occ = case_occ.component.occurrences.addNewComponent(identity)
        shell_occ.component.name = 'Front shell'
        shell = build_front_shell(shell_occ.component, p, d)

        cover_occ = case_occ.component.occurrences.addNewComponent(identity)
        cover_occ.component.name = 'Rear cover'
        cover = build_rear_cover(cover_occ.component, p, d)

        kit_occ = case_occ.component.occurrences.addNewComponent(identity)
        kit_occ.component.name = 'Kit envelope (reference)'
        kit = build_kit_envelope(kit_occ.component, p, d)

        stand_occ = root.occurrences.addNewComponent(identity)
        stand_occ.component.name = 'Desk stand'
        stand = build_stand(stand_occ.component, p, d)

        gauge_m = adsk.core.Matrix3D.create()
        gauge_m.translation = adsk.core.Vector3D.create(mm(d['sw'] / 2 + 15), 0, 0)
        gauge_occ = root.occurrences.addNewComponent(gauge_m)
        gauge_occ.component.name = 'Alignment gauge'
        gauge = build_gauge(gauge_occ.component, p)

        # --- verification in root context
        def proxy(body, path_occ):
            return body.createForAssemblyContext(path_occ)
        kids = {case_occ.childOccurrences.item(i).component.name: case_occ.childOccurrences.item(i)
                for i in range(case_occ.childOccurrences.count)}
        shell_p = proxy(shell, kids['Front shell'])
        cover_p = proxy(cover, kids['Rear cover'])
        kit_p = proxy(kit, kids['Kit envelope (reference)'])
        stand_p = proxy(stand, stand_occ)

        def interference(*bodies):
            ents = adsk.core.ObjectCollection.create()
            for b in bodies:
                ents.add(b)
            res = design.analyzeInterference(design.createInterferenceInput(ents))
            return round(sum(res.item(k).interferenceBody.volume for k in range(res.count)) * 1000, 4)

        gap = app.measureManager.measureMinimumDistance
        checks = {
            'shell_vs_cover_mm3': interference(shell_p, cover_p),
            'shell_vs_kit_mm3': interference(shell_p, kit_p),
            'cover_vs_kit_mm3': interference(cover_p, kit_p),
            'case_vs_stand_mm3': interference(shell_p, cover_p, stand_p),
            'case_to_stand_gap_mm': round(gap(shell_p, stand_p).value * 10, 3),
            'kit_to_cover_pads_gap_mm': round(gap(kit_p, cover_p).value * 10, 3),
        }

        os.makedirs(OUT_DIR, exist_ok=True)
        em = design.exportManager
        f3d = os.path.join(OUT_DIR, 'smart-picture-frame.f3d')
        step = os.path.join(OUT_DIR, 'smart-picture-frame.step')
        f3d_ok = em.execute(em.createFusionArchiveExportOptions(f3d))
        step_ok = em.execute(em.createSTEPExportOptions(step))
        app.activeViewport.fit()

        expected = {}
        vpath = os.path.join(HERE, 'validation.json')
        if os.path.exists(vpath):
            with open(vpath, encoding='utf-8') as f:
                expected = {k: v.get('volume_cm3') for k, v in json.load(f).items() if isinstance(v, dict)}
        return {
            'document': doc.name,
            'timeline_features': design.timeline.count,
            'user_parameters': up.count,
            'bodies': {
                'front_shell': {**metrics(shell), 'expected_volume_cm3': expected.get('front-shell')},
                'rear_cover': {**metrics(cover), 'expected_volume_cm3': expected.get('rear-cover')},
                'desk_stand': {**metrics(stand), 'expected_volume_cm3': expected.get('desk-stand')},
                'alignment_gauge': {**metrics(gauge), 'expected_volume_cm3': expected.get('alignment-gauge')},
                'kit_envelope': metrics(kit),
            },
            'checks': checks,
            'f3d': f3d if f3d_ok else None,
            'step': step if step_ok else None,
        }
    except Exception:
        print(traceback.format_exc())
        if __name__ != '__fusion_bridge__':
            ui.messageBox(f'Failed:\n{traceback.format_exc()}')
        raise
