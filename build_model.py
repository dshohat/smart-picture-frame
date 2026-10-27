"""Generate millimetre-scale printable solids and an offline WebGL preview."""

import json
import math
from pathlib import Path

import manifold3d as m
import numpy as np
import trimesh
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parent
P = json.loads((ROOT / "dimensions.json").read_text(encoding="utf-8"))
EPS = 0.02


def box(x, y, z, w, h, d):
    return m.Manifold.cube((w, h, d)).translate((x, y, z))


def rounded(x, y, z, w, h, d, r):
    if min(w, h) <= 2 * r:
        raise ValueError("Rounded rectangle radius is too large")
    section = m.CrossSection.square((w - 2 * r, h - 2 * r))
    section = section.offset(r, circular_segments=48).translate((x + r, y + r))
    return section.extrude(d).translate((0, 0, z))


def cylinder(x, y, z, diameter, height):
    return m.Manifold.cylinder(height, diameter / 2, diameter / 2, 48).translate((x, y, z))


def mesh(solid):
    if solid.status() != m.Error.NoError:
        raise ValueError(f"Solid kernel error: {solid.status()}")
    data = solid.to_mesh()
    result = trimesh.Trimesh(
        vertices=np.asarray(data.vert_properties)[:, :3],
        faces=np.asarray(data.tri_verts),
        process=True,
    )
    if not result.is_watertight or not result.is_winding_consistent or result.volume <= 0:
        raise ValueError("Generated mesh is not an outward-oriented watertight solid")
    if len(result.split()) != 1:
        raise ValueError("A printable part must be one connected solid")
    return result


def layout_drawing():
    """1:1 paper template plus a dimensioned drawing of the assumed board layout."""
    kw, kh = P["kit_width"], P["kit_height"]
    ww, wh = P["window_width"], P["window_height"]
    cx, cy = P["screen_center_from_kit_left"], P["screen_center_from_kit_bottom"]
    kx, ky, kd = P["camera_center_from_kit_left"], P["camera_center_from_kit_bottom"], P["camera_hole_diameter"]
    ox, oy = 14, 18  # kit origin on the page (top-left of the kit outline)

    def X(x):
        return ox + x

    def Y(y):  # kit y is measured from the bottom; SVG grows downward
        return oy + kh - y

    def dim_h(x0, x1, y, label, above=True):
        ty = y - 1 if above else y + 3.2
        return (f'<path d="M{x0:.2f} {y:.2f}H{x1:.2f}M{x0:.2f} {y - 1.5:.2f}v3M{x1:.2f} {y - 1.5:.2f}v3"/>'
                f'<text x="{(x0 + x1) / 2:.2f}" y="{ty:.2f}" text-anchor="middle" stroke="none">{label}</text>')

    def dim_v(y0, y1, x, label, left=True):
        tx = x - 1.2 if left else x + 1.2
        anchor = "end" if left else "start"
        return (f'<path d="M{x:.2f} {y0:.2f}V{y1:.2f}M{x - 1.5:.2f} {y0:.2f}h3M{x - 1.5:.2f} {y1:.2f}h3"/>'
                f'<text x="{tx:.2f}" y="{(y0 + y1) / 2 + 1:.2f}" text-anchor="{anchor}" stroke="none">{label}</text>')

    parts = []
    for c in P["assumed_layout"]:
        dash = ' stroke-dasharray="1 .7"' if c["name"] != "Active screen" else ""
        parts.append(f'<rect x="{X(c["x"]):.2f}" y="{Y(c["y"] + c["h"]):.2f}" width="{c["w"]}" height="{c["h"]}"'
                     f' fill="none" stroke="#777" stroke-width=".25"{dash}/>')
        pos = c.get("label_pos", "inside")
        if pos == "below":
            lx, ly = X(c["x"]), Y(c["y"]) + 2.4
        elif pos == "above":
            lx, ly = X(c["x"]), Y(c["y"] + c["h"]) - 0.7
        else:
            lx, ly = X(c["x"]) + 1, Y(c["y"] + c["h"]) + 2.6
        parts.append(f'<text x="{lx:.2f}" y="{ly:.2f}" font-size="2" fill="#555">'
                     f'{c["name"]} {c["w"]:g}&#215;{c["h"]:g}</text>')
    sx, sy = cx - ww / 2, cy - wh / 2
    dims = "".join([
        dim_h(X(0), X(kw), oy - 6, f"{kw:g} (kit width)"),
        dim_v(Y(kh), Y(0), ox - 7, f"{kh:g}"),
        dim_h(X(0), X(sx), Y(0) + 6, f"{sx:.1f}", above=False),
        dim_h(X(sx), X(sx + ww), Y(0) + 6, f"window {ww:g}", above=False),
        dim_h(X(sx + ww), X(kw), Y(0) + 6, f"{kw - sx - ww:.1f}", above=False),
        dim_v(Y(sy), Y(0), X(kw) + 7, f"{sy:.1f}", left=False),
        dim_v(Y(sy + wh), Y(sy), X(kw) + 7, f"window {wh:g}", left=False),
        dim_v(Y(kh), Y(sy + wh), X(kw) + 7, f"{kh - sy - wh:.1f}", left=False),
        dim_h(X(0), X(cx), Y(cy), f"screen centre {cx:g} from left"),
        dim_v(Y(cy), Y(0), X(cx), f"{cy:g} from bottom", left=False),
        dim_h(X(0), X(kx), Y(ky), f"camera {kx:g}"),
        dim_v(Y(ky), Y(0), X(kx), f"{ky:g}", left=True),
    ])
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" width="150mm" height="175mm" viewBox="0 0 150 175" '
        'font-family="sans-serif" font-size="2.6">\n'
        '<rect width="150" height="175" fill="white"/>\n'
        f'<text x="{ox}" y="4.5" font-size="3.4" font-weight="bold">Freenove FNK0086 - assumed layout, 1:1 (mm). TOP / speakers up.</text>\n'
        '<g stroke="black" stroke-width=".35" fill="none">\n'
        f'<rect x="{X(0)}" y="{Y(kh)}" width="{kw}" height="{kh}" rx="2"/>\n'
        f'<rect x="{X(sx):.2f}" y="{Y(sy + wh):.2f}" width="{ww}" height="{wh}" rx=".8" stroke="#c0392b" stroke-width=".5"/>\n'
        f'<circle cx="{X(kx):.2f}" cy="{Y(ky):.2f}" r="{kd / 2:.2f}" stroke="#c0392b" stroke-width=".5"/>\n'
        '</g>\n'
        f'{"".join(parts)}\n'
        f'<g stroke="#1f5fa8" stroke-width=".25" fill="#1f5fa8">{dims}</g>\n'
        '<g font-size="2.4" fill="#333">\n'
        f'<text x="{ox}" y="{Y(0) + 13}">Red = frame window and camera hole (cut out). Solid grey = active screen as measured. Dashed = other parts, approximate.</text>\n'
        f'<text x="{ox}" y="{Y(0) + 17}">Lay this on the unpowered kit: the outline must match the board edge, the whole lit screen must sit inside the red window, the lens inside the circle.</text>\n'
        f'<text x="{ox}" y="{Y(0) + 21}">If anything is off, note the mm difference, change dimensions.json and rerun build_model.py.</text>\n'
        '</g>\n'
        f'<g stroke="black" stroke-width=".35" fill="none"><path d="M{ox} {Y(0) + 27}h50m-50 -2v4m50 -4v4"/></g>\n'
        f'<text x="{ox + 52}" y="{Y(0) + 28}">50 mm calibration - print at 100%, no fit-to-page.</text>\n'
        '</svg>\n'
    )
    (ROOT / "alignment-template.svg").write_text(svg, encoding="utf-8")


def layout_overlay():
    """Draw the assumed layout over the kit illustration so it can be checked by eye."""
    source = ROOT.parent / "Development_Kit.jpg"
    if not source.is_file():
        return
    im = Image.open(source).convert("RGB")
    bx0, bx1, by0, by1 = 7, 1882, 8, 2592  # board outline in the illustration, px
    sx = (bx1 - bx0) / P["kit_width"]
    sy = (by1 - by0) / P["kit_height"]
    d = ImageDraw.Draw(im)
    for c in P["assumed_layout"]:
        x0 = bx0 + c["x"] * sx
        x1 = bx0 + (c["x"] + c["w"]) * sx
        y0 = by1 - (c["y"] + c["h"]) * sy
        y1 = by1 - c["y"] * sy
        color = (220, 40, 40) if c["name"] == "Active screen" else (40, 110, 230)
        d.rectangle([x0, y0, x1, y1], outline=color, width=6)
        d.text((x0 + 8, y0 + 8), f'{c["name"]} {c["w"]:g}x{c["h"]:g} @ ({c["x"]:g},{c["y"]:g})', fill=color)
    ww, wh = P["window_width"], P["window_height"]
    wx = P["screen_center_from_kit_left"] - ww / 2
    wy = P["screen_center_from_kit_bottom"] - wh / 2
    d.rectangle([bx0 + wx * sx, by1 - (wy + wh) * sy, bx0 + (wx + ww) * sx, by1 - wy * sy],
                outline=(255, 140, 0), width=8)
    d.text((bx0 + wx * sx + 8, by1 - wy * sy + 10), f"frame window {ww:g} x {wh:g} mm", fill=(255, 140, 0))
    kx, ky, kr = P["camera_center_from_kit_left"], P["camera_center_from_kit_bottom"], P["camera_hole_diameter"] / 2
    d.ellipse([bx0 + (kx - kr) * sx, by1 - (ky + kr) * sy, bx0 + (kx + kr) * sx, by1 - (ky - kr) * sy],
              outline=(255, 140, 0), width=8)
    d.text((bx0 + (kx + kr) * sx + 8, by1 - ky * sy), f"camera hole d{2 * kr:g} @ ({kx:g},{ky:g})", fill=(255, 140, 0))
    for mm in range(0, int(P["kit_width"]) + 1, 10):
        x = bx0 + mm * sx
        d.line([(x, by0), (x, by0 + 40)], fill=(0, 0, 0), width=3)
        d.text((x + 4, by0 + 44), f"{mm}", fill=(0, 0, 0))
    for mm in range(0, int(P["kit_height"]) + 1, 10):
        y = by1 - mm * sy
        d.line([(bx0, y), (bx0 + 40, y)], fill=(0, 0, 0), width=3)
        d.text((bx0 + 44, y - 14), f"{mm}", fill=(0, 0, 0))
    (ROOT / "reference").mkdir(exist_ok=True)
    im.save(ROOT / "reference" / "assumed-layout-overlay.jpg", quality=88)


def build():
    wall, face = P["wall"], P["front_thickness"]
    depth, radius = P["shell_depth"], P["corner_radius"]
    ww, wh = P["window_width"], P["window_height"]
    kw, kh = P["kit_width"], P["kit_height"]
    # The case is sized from the kit: side walls hug it; the bottom band holds the
    # USB plug and two screws, the top band holds the other two screws.
    bx = wall + P["side_clearance"]
    by = wall + P["bottom_band"]
    w = bx + kw + P["side_clearance"] + wall
    h = by + kh + P["top_band"] + wall
    case_depth = depth + P["lid_thickness"]
    board_back = face + P["screen_recess"] + P["kit_depth"]
    pad_top = board_back + P["rear_pad_thickness"]
    boss_d = P["screw_boss_diameter"]
    if pad_top >= depth - 3:
        raise ValueError("Insufficient rear clearance for supports and lid")
    if not (0 < P["lid_clearance"] < 1 and wall >= 2):
        raise ValueError("Invalid print clearances or wall thickness")
    if P["top_band"] < boss_d - 1 + 1.5 or P["bottom_band"] < boss_d - 1 + 1.5:
        raise ValueError("Screw bosses do not fit in the top/bottom bands")
    if radius <= wall + P["lid_clearance"] + 1.6:
        raise ValueError("Corner radius too small for the lid tongue")
    wx = bx + P["screen_center_from_kit_left"] - ww / 2
    wy = by + P["screen_center_from_kit_bottom"] - wh / 2
    # OV2640 lens on the ESP32-S3 WROOM module: the 9 x 9 mm holder stands 6.45 mm
    # above the module shield, which is roughly level with the display glass, so the
    # barrel passes through the face and the square base reaches its inside surface.
    cam_x = bx + P["camera_center_from_kit_left"]
    cam_y = by + P["camera_center_from_kit_bottom"]
    cam_d, cam_sq, cam_rd = P["camera_hole_diameter"], P["camera_relief_size"], P["camera_relief_depth"]
    if not (cam_d < cam_sq and 0 < cam_rd < face - 0.6):
        raise ValueError("Camera relief must be wider than the hole and leave >= 0.6 mm of face")
    if cam_x + cam_sq / 2 > wx - 2 and cam_y + cam_sq / 2 > wy - 2 and cam_y - cam_sq / 2 < wy + wh + 2:
        raise ValueError("Camera hole runs into the screen window")
    if cam_x - cam_sq / 2 < wall + 1 or cam_y - cam_sq / 2 < by or cam_y + cam_sq / 2 > by + kh:
        raise ValueError("Camera hole is not over the kit area inside the walls")

    # USB plugs enter from the kit's bottom-left edge, so no boss may sit there.
    # Bosses are embedded 1 mm into the walls so they fuse cleanly (no tangent faces).
    usb_x0 = bx + P["usb_bay_from_kit_left"]
    usb_x1 = usb_x0 + P["usb_bay_width"]
    r = boss_d / 2
    screws = [(usb_x1 + r + 1, wall + r - 1),
              (w - wall - r + 1, wall + r - 1),
              (wall + r - 1, h - wall - r + 1),
              (w - wall - r + 1, h - wall - r + 1)]
    shell = rounded(0, 0, 0, w, h, depth, radius)
    cavity = rounded(wall, wall, face, w - 2 * wall, h - 2 * wall,
                     depth + EPS, radius - wall)
    for x, y in screws:
        cavity -= cylinder(x, y, face - 1, boss_d, depth + 2)
    shell -= cavity
    aperture = rounded(wx, wy, -EPS, ww, wh, face + 2 * EPS, P["window_radius"])
    shell -= aperture
    # A wider inside mouth avoids touching the LCD bezel around the visible opening.
    shell -= rounded(wx - 1, wy - 1, face - 0.8, ww + 2, wh + 2,
                     1, P["window_radius"])
    shell -= cylinder(cam_x, cam_y, -EPS, cam_d, face + 2 * EPS)
    shell -= box(cam_x - cam_sq / 2, cam_y - cam_sq / 2, face - cam_rd, cam_sq, cam_sq, cam_rd + 1)
    for x, y in screws:
        shell -= cylinder(x, y, depth - 12, P["screw_pilot_diameter"], 12 + EPS)

    lid = rounded(0, 0, depth, w, h, P["lid_thickness"], radius)
    gap = P["lid_clearance"]
    tx, ty = wall + gap, wall + gap
    tw, th = w - 2 * tx, h - 2 * ty
    tongue = rounded(tx, ty, depth - 2, tw, th, 2 + EPS, radius - tx)
    tongue -= rounded(tx + 1.6, ty + 1.6, depth - 2 - EPS,
                      tw - 3.2, th - 3.2, 2 + 3 * EPS, radius - tx - 1.6)
    for x, y in screws:
        tongue -= cylinder(x, y, depth - 3, boss_d + 0.8, 5)
    lid += tongue
    for x, y in screws:
        lid -= cylinder(x, y, depth - 3, P["screw_clearance_diameter"], 7)
        lid -= cylinder(x, y, depth + P["lid_thickness"] - 1.2, 6.4, 2)

    # No guessed PCB mounting-hole pattern: padded rear corner seats plus loose guides.
    pads = [(bx + 4, by + 4), (bx + kw - 4, by + 4),
            (bx + 4, by + kh - 4), (bx + kw - 4, by + kh - 4)]
    for x, y in pads:
        lid += rounded(x - 3, y - 3, pad_top, 6, 6, depth - pad_top + EPS, 1)
    # With 1 mm side clearance the shell walls locate the kit; no extra guide posts.
    # Cable exit through the back, behind the USB bay in the bottom band.
    bay = (usb_x0 + 1, wall + 1, P["usb_bay_width"] - 2, P["bottom_band"] - 2)
    lid -= rounded(bay[0], bay[1], depth - 3, bay[2], bay[3], 7, 3)
    # Vent slots over the kit, clear of pads and guides.
    for y in [by + 34, by + kh - 56]:
        x = bx + 10
        while x + 3.2 <= bx + kw - 10:
            lid -= rounded(x, y, depth - 3, 3.2, 22, 7, 1.5)
            x += 8

    sw, sd, sh = w + 2 * P["stand_margin"], P["stand_depth"], P["stand_height"]
    stand = rounded(0, 0, 0, sw, sd, sh, 6)
    slot_w = w + P["stand_clearance"]
    slot_d = case_depth + P["stand_clearance"]
    slot_y = 18
    slot = box(0, 0, 0, slot_w, slot_d, 60)
    angle = math.radians(P["stand_angle"])
    slot_z = 6 + math.sin(angle) * slot_d
    slot = slot.rotate((-P["stand_angle"], 0, 0)).translate(((sw - slot_w) / 2, slot_y, slot_z))
    stand -= slot
    # Notch through the rear of the stand so the USB cable can leave the back of the case.
    notch_x = (sw - slot_w) / 2 + usb_x0 - 1
    stand -= box(notch_x, slot_y + slot_d * 0.55, 6, P["usb_bay_width"] + 2, sd, sh)
    clearance = P["stand_clearance"] / 2
    seated_case = (shell + lid).mirror((0, 0, 1)).rotate((90 - P["stand_angle"], 0, 0))
    seated_case = seated_case.translate(((sw - w) / 2,
                                        slot_y + math.cos(angle) * clearance,
                                        slot_z - math.sin(angle) * clearance))
    stand_intersection = (seated_case ^ stand).volume()
    if stand_intersection > 0.001:
        raise ValueError(f"Case and stand interfere: {stand_intersection} cubic mm")
    kit_env = box(bx, by, face + P["screen_recess"], kw, kh, P["kit_depth"])
    if (shell ^ kit_env).volume() > 0.001:
        raise ValueError("Kit envelope collides with shell")
    if (lid ^ kit_env).volume() > 0.001:
        raise ValueError("Kit envelope collides with rear cover")
    # USB plug envelope: from the socket mouths down into the bottom band, full kit depth.
    plug_env = box(usb_x0, wall + 1, face + P["screen_recess"],
                   P["usb_bay_width"], by - wall - 1 + EPS, P["kit_depth"])
    if (shell ^ plug_env).volume() > 0.001 or (lid ^ plug_env).volume() > 0.001:
        raise ValueError("USB plug bay is obstructed")

    gauge = rounded(0, 0, 0, kw, kh, 0.8, 2)
    gauge -= rounded(P["screen_center_from_kit_left"] - ww / 2,
                     P["screen_center_from_kit_bottom"] - wh / 2,
                     -EPS, ww, wh, 1, P["window_radius"])
    gauge -= cylinder(P["camera_center_from_kit_left"], P["camera_center_from_kit_bottom"], -EPS, cam_d, 1)
    solids = {"front-shell": shell, "rear-cover": lid, "desk-stand": stand,
              "alignment-gauge": gauge}
    assembled = {name: mesh(solid) for name, solid in solids.items()}
    if (shell ^ lid).volume() > 0.001:
        raise ValueError("Shell and rear cover interfere")
    if mesh(stand).bounds[1, 2] > sh + 0.01:
        raise ValueError("Stand bounds incorrect")

    output = ROOT / "print"
    output.mkdir(exist_ok=True)
    report = {}
    preview = {}
    # Model coordinates are front-view drawing coordinates (x to the viewer's right,
    # y up, z into the case). That triple is left-handed, so the case parts must be
    # reflected in X to obtain the physical, right-handed parts; the stand is already
    # modelled in a physical frame (x right, y back, z up) and is not reflected.
    reflect_x = np.diag([-1.0, 1.0, 1.0, 1.0])
    for name, original in assembled.items():
        printable = original.copy()
        if name != "desk-stand":
            printable.apply_transform(reflect_x)
        if name == "rear-cover":
            # Outer back face goes on the print bed; supports grow upward.
            printable.apply_transform(trimesh.transformations.rotation_matrix(math.pi, [1, 0, 0]))
        printable.apply_translation(-printable.bounds[0])
        path = output / f"{name}.stl"
        printable.export(path)
        reloaded = trimesh.load_mesh(path)
        if not reloaded.is_watertight or not reloaded.is_volume:
            raise ValueError(f"STL round-trip validation failed: {name}")
        if reloaded.volume < 0 or abs(reloaded.volume - original.volume) > 0.05 * original.volume:
            raise ValueError(f"STL volume changed on export: {name}")
        report[name] = {
            "size_mm": np.round(printable.extents, 3).tolist(),
            "volume_cm3": round(printable.volume / 1000, 3),
            "triangles": len(printable.faces),
            "watertight": bool(reloaded.is_watertight),
            "connected_solids": len(reloaded.split()),
            "print_bed_min_z": round(float(printable.bounds[0, 2]), 6),
        }
        if name != "alignment-gauge":
            preview[name] = {
                "vertices": np.round(original.vertices, 4).tolist(),
                "faces": original.faces.tolist(),
            }

    derived = {"case_width": round(w, 2), "case_height": round(h, 2), "case_depth": round(case_depth, 2),
               "stand_width": round(sw, 2), "slot_y": slot_y,
               "kit_origin": [round(bx, 2), round(by, 2), face + P["screen_recess"]],
               "screw_positions": [[round(x, 2), round(y, 2)] for x, y in screws]}
    preview["kit"] = {"origin": derived["kit_origin"], "size": [kw, kh, P["kit_depth"]]}
    preview["derived"] = derived
    report["assembly"] = {
        **derived,
        "shell_lid_intersection_mm3": round((shell ^ lid).volume(), 6),
        "case_stand_intersection_mm3": round(stand_intersection, 6),
        "kit_rear_mm": board_back,
        "screen_position_status": "measured from the kit illustration scaled to 85 x 112 mm; verify with the template",
        "support_seat_status": "inferred rear corner locations; inspect before mounting",
        "units": "mm",
    }
    (ROOT / "validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (ROOT / "model-data.json").write_text(json.dumps(preview, separators=(",", ":")), encoding="utf-8")
    template = (ROOT / "preview-template.html").read_text(encoding="utf-8")
    html = template.replace("__MODEL_DATA__", json.dumps(preview, separators=(",", ":")))
    html = html.replace("__PARAMETERS__", json.dumps(P, separators=(",", ":")))
    (ROOT / "index.html").write_text(html, encoding="utf-8")
    layout_drawing()
    layout_overlay()
    print(json.dumps(report["assembly"], indent=2))
    for name in solids:
        print(name, report[name]["size_mm"], "watertight" if report[name]["watertight"] else "BAD")


if __name__ == "__main__":
    build()
