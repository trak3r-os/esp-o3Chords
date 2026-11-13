from __future__ import annotations

"""
CadQuery model of the split o3Chords enclosure: a base and two lids (left lid
carries the joystick opening and the OLED window, right lid the keypad).

    python3 enclosure.py --part all --format stl     # export to ./stl
    python3 enclosure.py --part all --viewer         # preview in OCP CAD Viewer
"""

import argparse
import importlib.util
import subprocess
import sys
import time
import types
from dataclasses import dataclass
from math import radians, tan
from pathlib import Path


def _install_ivtk_stubs_if_needed() -> None:
    # Some CadQuery Python environments ship without the IVtk helper modules.
    # They are only needed for VTK display paths, not for solid generation.
    missing = set()
    for module_name in ("OCP.IVtkOCC", "OCP.IVtkVTK"):
        try:
            if importlib.util.find_spec(module_name) is None:
                missing.add(module_name)
        except ModuleNotFoundError:
            missing.add(module_name)

    if "OCP.IVtkOCC" in missing:
        mod = types.ModuleType("OCP.IVtkOCC")

        class IVtkOCC_Shape:
            def __init__(self, *args, **kwargs):
                pass

        class IVtkOCC_ShapeMesher:
            def __init__(self, *args, **kwargs):
                pass

            def Build(self, *args, **kwargs):
                pass

        mod.IVtkOCC_Shape = IVtkOCC_Shape
        mod.IVtkOCC_ShapeMesher = IVtkOCC_ShapeMesher
        sys.modules["OCP.IVtkOCC"] = mod

    if "OCP.IVtkVTK" in missing:
        mod = types.ModuleType("OCP.IVtkVTK")

        class IVtkVTK_ShapeData:
            pass

        mod.IVtkVTK_ShapeData = IVtkVTK_ShapeData
        sys.modules["OCP.IVtkVTK"] = mod


_install_ivtk_stubs_if_needed()

import cadquery as cq
from cadquery import exporters
from cadquery.occ_impl import shapes as cq_shapes


def _patch_shape_hash() -> None:
    def _safe_hash_code(self) -> int:
        try:
            return self.wrapped.HashCode(2147483647)
        except Exception:
            return hash(self.wrapped)

    cq_shapes.Shape.hashCode = _safe_hash_code
    cq_shapes.Shape.__hash__ = lambda self: _safe_hash_code(self)


_patch_shape_hash()


@dataclass(frozen=True)
class EnclosureParams:
    wall: float = 3.0
    floor_th: float = 3.0
    lid_th: float = 3.0
    outer_y: float = 120.0
    left_w: float = 64.0
    right_w: float = 90.0
    div_wall: float = 3.0
    seam: float = 0.2
    height_scale: float = 0.72

    left_front_nom: float = 20.0
    left_back_nom: float = 68.0
    right_front_nom: float = 66.0
    right_back_nom: float = 81.0

    lid_fit_gap: float = 0.25
    lid_tongue_wall: float = 1.6
    lid_tongue_depth: float = 2.8

    screw_inset: float = 8.0
    left_inner_screw_inset: float = 10.0
    right_inner_screw_inset: float = 6.0
    screw_od: float = 8.0
    screw_tap_d: float = 2.5
    screw_clear_d: float = 3.4
    screw_head_d: float = 5.8
    screw_head_depth: float = 2.0

    js_center_y: float = 42.0
    js_board_x: float = 32.0
    js_board_y: float = 24.0
    # Corner-hole pitch estimated from the 32 x 24 module outline you supplied.
    js_mount_pitch_x: float = 27.0
    js_mount_pitch_y: float = 19.0
    js_support_h: float = 6.5
    js_support_od: float = 8.5
    js_support_tap_d: float = 2.5
    # The lid crosses the lower joystick cup, not the thin neck, so the opening
    # must follow the cup sweep at the lid planes.
    js_board_th: float = 1.6
    js_body_h: float = 11.0
    js_sweep_d: float = 18.2
    js_sweep_clear: float = 0.15
    js_max_angle_deg: float = 25.0
    js_through_margin: float = 0.5
    js_face_relief_d: float = 30.0
    js_face_relief_depth: float = 1.5

    # 0.96" OLED based on your supplied breakout drawing, mounted in landscape.
    # Board and display-window dimensions follow the drawing directly; only the
    # corner-hole pitch remains an estimate until measured on the real board.
    oled_back_clear: float = 14.0
    oled_min_js_gap: float = 14.0
    oled_board_x: float = 27.3
    oled_board_y: float = 27.8
    oled_window_x: float = 22.2
    oled_window_y: float = 11.3
    oled_window_r: float = 1.4
    oled_window_offset_x: float = 0.0
    oled_window_offset_y: float = -1.4
    oled_bezel_x: float = 27.0
    oled_bezel_y: float = 19.6
    oled_bezel_r: float = 2.0
    oled_bezel_depth: float = 0.7
    oled_pocket_depth: float = 1.6
    oled_dock_gap: float = 0.25
    oled_wire_notch_x: float = 15.0
    oled_wire_notch_y: float = 6.5
    # Estimated from the common 4-corner breakout layout, not from a hole table.
    oled_hole_pitch_x: float = 21.8
    oled_hole_pitch_y: float = 23.8
    oled_screw_clear_d: float = 2.2
    oled_screw_head_d: float = 4.4
    oled_screw_head_depth: float = 1.4

    # Adafruit PID 3844 panel-mount keypad
    # Official product dimensions: 69.0 mm x 65.5 mm x 9.7 mm.
    kp_outer_x: float = 69.4
    kp_outer_y: float = 65.9
    kp_outer_r: float = 4.2
    kp_recess_depth: float = 1.6
    kp_body_cut_x: float = 60.8
    kp_body_cut_y: float = 57.5
    kp_body_cut_r: float = 3.2
    kp_tail_slot_x: float = 24.0
    kp_tail_slot_y: float = 12.0
    kp_tail_slot_back_offset: float = 2.5
    kp_mount_pitch_x: float = 60.5
    kp_mount_pitch_y: float = 57.0
    kp_mount_hole_d: float = 3.2
    kp_mount_counterbore_d: float = 6.0
    kp_mount_counterbore_depth: float = 1.8

    usbc_w: float = 13.0
    usbc_h: float = 6.0
    usbc_center_z: float = 16.0

    carrier_x: float = 70.0
    carrier_y: float = 80.0
    carrier_hole_offset_x: float = 14.7
    carrier_hole_offset_y: float = 2.0
    carrier_standoff_h: float = 8.0
    carrier_standoff_od: float = 7.5
    carrier_standoff_hd: float = 3.0

    cable_slot_w: float = 42.0
    cable_slot_h: float = 20.0
    cable_slot_z_offset: float = 2.0

    vent_count: int = 5
    vent_w: float = 2.0
    vent_l: float = 40.0
    vent_spacing: float = 8.0

    @property
    def outer_x(self) -> float:
        return self.wall + self.left_w + self.div_wall + self.right_w + self.wall

    @property
    def div_x(self) -> float:
        return self.wall + self.left_w

    @property
    def left_front_h(self) -> float:
        return self.left_front_nom * self.height_scale

    @property
    def left_back_h(self) -> float:
        return self.left_back_nom * self.height_scale

    @property
    def right_front_h(self) -> float:
        return self.right_front_nom * self.height_scale

    @property
    def right_back_h(self) -> float:
        return self.right_back_nom * self.height_scale

    @property
    def left_lid_width(self) -> float:
        return self.wall + self.left_w + self.div_wall / 2.0

    @property
    def right_lid_x0(self) -> float:
        return self.wall + self.left_w + self.div_wall / 2.0

    @property
    def right_lid_width(self) -> float:
        return self.div_wall / 2.0 + self.right_w + self.wall

    @property
    def right_zone_x0(self) -> float:
        return self.div_x + self.div_wall

    @property
    def js_center_x(self) -> float:
        return self.wall + self.left_w / 2.0

    @property
    def js_stem_base_z(self) -> float:
        return self.floor_th + self.js_support_h + self.js_board_th + self.js_body_h

    @property
    def js_support_back_y(self) -> float:
        return self.js_center_y + self.js_mount_pitch_y / 2.0 + self.js_support_od / 2.0

    @property
    def oled_center_x(self) -> float:
        return self.wall + self.left_w / 2.0

    @property
    def oled_center_y(self) -> float:
        oled_target = self.outer_y - self.wall - self.oled_back_clear - self.oled_board_y / 2.0
        return max(oled_target, self.js_support_back_y + self.oled_min_js_gap + self.oled_board_y / 2.0)

    @property
    def oled_window_center_x(self) -> float:
        return self.oled_center_x + self.oled_window_offset_x

    @property
    def oled_window_center_y(self) -> float:
        return self.oled_center_y + self.oled_window_offset_y

    @property
    def oled_pocket_x(self) -> float:
        return self.oled_board_x + 2.0 * self.oled_dock_gap

    @property
    def oled_pocket_y(self) -> float:
        return self.oled_board_y + 2.0 * self.oled_dock_gap

    def oled_hole_positions(self) -> list[tuple[float, float]]:
        dx = self.oled_hole_pitch_x / 2.0
        dy = self.oled_hole_pitch_y / 2.0
        return [
            (self.oled_center_x - dx, self.oled_center_y - dy),
            (self.oled_center_x - dx, self.oled_center_y + dy),
            (self.oled_center_x + dx, self.oled_center_y - dy),
            (self.oled_center_x + dx, self.oled_center_y + dy),
        ]

    @property
    def kp_center_x(self) -> float:
        return self.div_x + self.div_wall + self.right_w / 2.0

    @property
    def kp_center_y(self) -> float:
        return self.outer_y / 2.0

    @property
    def carrier_origin_x(self) -> float:
        return self.div_x + self.div_wall + (self.right_w - self.carrier_x) / 2.0

    @property
    def carrier_origin_y(self) -> float:
        return (self.outer_y - self.carrier_y) / 2.0

    @property
    def carrier_hole_origin_x(self) -> float:
        return self.carrier_origin_x + self.carrier_hole_offset_x

    @property
    def carrier_hole_origin_y(self) -> float:
        return self.carrier_origin_y + self.carrier_hole_offset_y

    @property
    def carrier_hole_pitch_x(self) -> float:
        return self.carrier_x - 2.0 * self.carrier_hole_offset_x

    @property
    def carrier_hole_pitch_y(self) -> float:
        return self.carrier_y - 2.0 * self.carrier_hole_offset_y

    @property
    def cable_slot_z(self) -> float:
        return self.floor_th + self.cable_slot_z_offset

    @property
    def left_inner_screw_x(self) -> float:
        return self.div_x - self.left_inner_screw_inset

    @property
    def right_inner_screw_x(self) -> float:
        return self.right_zone_x0 + self.right_inner_screw_inset

    def left_top(self, y: float) -> float:
        return self.left_front_h + (self.left_back_h - self.left_front_h) * (y / self.outer_y)

    def right_top(self, y: float) -> float:
        return self.right_front_h + (self.right_back_h - self.right_front_h) * (y / self.outer_y)

    def left_under(self, y: float) -> float:
        return self.left_top(y) - self.lid_th

    def right_under(self, y: float) -> float:
        return self.right_top(y) - self.lid_th

    def left_screw_positions(self) -> list[tuple[float, float]]:
        return [
            (self.screw_inset, self.screw_inset),
            (self.screw_inset, self.outer_y - self.screw_inset),
            (self.left_inner_screw_x, self.screw_inset),
            (self.left_inner_screw_x, self.outer_y - self.screw_inset),
        ]

    def right_screw_positions(self) -> list[tuple[float, float]]:
        return [
            (self.right_inner_screw_x, self.screw_inset),
            (self.right_inner_screw_x, self.outer_y - self.screw_inset),
            (self.outer_x - self.screw_inset, self.screw_inset),
            (self.outer_x - self.screw_inset, self.outer_y - self.screw_inset),
        ]

    def all_screw_positions(self) -> list[tuple[float, float]]:
        return self.left_screw_positions() + self.right_screw_positions()


def fuse_all(shapes: list[cq.Workplane]) -> cq.Workplane:
    if not shapes:
        raise ValueError("fuse_all() requires at least one shape")
    result = shapes[0]
    for shape in shapes[1:]:
        result = result.union(shape)
    return result


def cut_all(base: cq.Workplane, cuts: list[cq.Workplane]) -> cq.Workplane:
    result = base
    for cutter in cuts:
        result = result.cut(cutter)
    return result


def wedge(x0: float, width: float, y0: float, depth: float, h_front: float, h_back: float, z0: float = 0.0) -> cq.Workplane:
    hf = max(h_front, 0.01)
    hb = max(h_back, 0.01)
    return (
        cq.Workplane("YZ", origin=(x0, y0, z0))
        .polyline([(0.0, 0.0), (depth, 0.0), (depth, hb), (0.0, hf)])
        .close()
        .extrude(width)
    )


def sloped_slab(x0: float, width: float, y0: float, depth: float, top_front: float, top_back: float, thickness: float) -> cq.Workplane:
    outer = wedge(x0, width, y0, depth, top_front, top_back, 0.0)
    inner = wedge(x0, width, y0, depth, top_front - thickness, top_back - thickness, 0.0)
    return outer.cut(inner)


def sloped_block_centered(xc: float, yc: float, sx: float, sy: float, top_fn, thickness: float) -> cq.Workplane:
    y0 = yc - sy / 2.0
    y1 = yc + sy / 2.0
    return sloped_slab(xc - sx / 2.0, sx, y0, sy, top_fn(y0), top_fn(y1), thickness)


def box_from_z(xc: float, yc: float, z0: float, sx: float, sy: float, sz: float) -> cq.Workplane:
    return cq.Workplane("XY", origin=(xc, yc, z0)).box(sx, sy, sz, centered=(True, True, False))


def cylinder_from_z(xc: float, yc: float, z0: float, height: float, diameter: float) -> cq.Workplane:
    return cq.Workplane("XY", origin=(xc, yc, z0)).circle(diameter / 2.0).extrude(height)


def circular_frustum_from_z(xc: float, yc: float, z0: float, height: float, d0: float, d1: float) -> cq.Workplane:
    return (
        cq.Workplane("XY", origin=(xc, yc, z0))
        .circle(d0 / 2.0)
        .workplane(offset=height)
        .circle(d1 / 2.0)
        .loft(combine=True)
    )


def rounded_rect_prism(xc: float, yc: float, z0: float, sx: float, sy: float, radius: float, height: float) -> cq.Workplane:
    r = max(0.0, min(radius, sx / 2.0, sy / 2.0))
    if r <= 0.0:
        return box_from_z(xc, yc, z0, sx, sy, height)

    pieces: list[cq.Workplane] = []
    if sx - 2.0 * r > 0.0:
        pieces.append(box_from_z(xc, yc, z0, sx - 2.0 * r, sy, height))
    if sy - 2.0 * r > 0.0:
        pieces.append(box_from_z(xc, yc, z0, sx, sy - 2.0 * r, height))

    corner_dx = sx / 2.0 - r
    corner_dy = sy / 2.0 - r
    for dx in (-corner_dx, corner_dx):
        for dy in (-corner_dy, corner_dy):
            pieces.append(cylinder_from_z(xc + dx, yc + dy, z0, height, 2.0 * r))

    return fuse_all(pieces)


def joystick_aperture_diameter_at_z(p: EnclosureParams, z: float) -> float:
    tilt = tan(radians(p.js_max_angle_deg))
    lateral = max(0.0, z - p.js_stem_base_z) * tilt
    return p.js_sweep_d + 2.0 * (lateral + p.js_sweep_clear)


def joystick_aperture_diameters(p: EnclosureParams) -> tuple[float, float]:

    js_under_z = p.left_under(p.js_center_y)
    js_top_z = p.left_top(p.js_center_y)
    return joystick_aperture_diameter_at_z(p, js_under_z), joystick_aperture_diameter_at_z(p, js_top_z)


def build_base(p: EnclosureParams) -> cq.Workplane:
    outer = fuse_all(
        [
            wedge(
                0.0,
                p.left_lid_width + p.seam,
                0.0,
                p.outer_y,
                p.left_front_h - p.lid_th,
                p.left_back_h - p.lid_th,
                0.0,
            ),
            wedge(
                p.right_lid_x0 - p.seam,
                p.right_lid_width + p.seam,
                0.0,
                p.outer_y,
                p.right_front_h - p.lid_th,
                p.right_back_h - p.lid_th,
                0.0,
            ),
        ]
    )

    left_inner = wedge(
        p.wall,
        p.left_w,
        p.wall,
        p.outer_y - 2.0 * p.wall,
        p.left_top(p.wall) - p.lid_th - p.floor_th + p.seam,
        p.left_top(p.outer_y - p.wall) - p.lid_th - p.floor_th + p.seam,
        p.floor_th,
    )
    right_inner = wedge(
        p.right_zone_x0,
        p.right_w,
        p.wall,
        p.outer_y - 2.0 * p.wall,
        p.right_top(p.wall) - p.lid_th - p.floor_th + p.seam,
        p.right_top(p.outer_y - p.wall) - p.lid_th - p.floor_th + p.seam,
        p.floor_th,
    )

    base = outer.cut(left_inner).cut(right_inner)

    posts: list[cq.Workplane] = []
    for x, y in p.all_screw_positions():
        zone_h = p.left_under(y) if x < p.div_x + p.div_wall / 2.0 else p.right_under(y)
        posts.append(cylinder_from_z(x, y, p.floor_th, zone_h - p.floor_th, p.screw_od))

    standoffs: list[cq.Workplane] = []
    for sx in (0, 1):
        for sy in (0, 1):
            x = p.carrier_hole_origin_x + sx * p.carrier_hole_pitch_x
            y = p.carrier_hole_origin_y + sy * p.carrier_hole_pitch_y
            outer_standoff = cylinder_from_z(x, y, p.floor_th, p.carrier_standoff_h, p.carrier_standoff_od)
            inner_hole = cylinder_from_z(x, y, p.floor_th - 0.2, p.carrier_standoff_h + 0.4, p.carrier_standoff_hd)
            standoffs.append(outer_standoff.cut(inner_hole))

    js_supports: list[cq.Workplane] = []
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            x = p.js_center_x + sx * p.js_mount_pitch_x / 2.0
            y = p.js_center_y + sy * p.js_mount_pitch_y / 2.0
            js_supports.append(cylinder_from_z(x, y, p.floor_th, p.js_support_h, p.js_support_od))

    base = fuse_all([base] + posts + standoffs + js_supports)

    cuts: list[cq.Workplane] = []
    cuts.append(
        box_from_z(
            p.div_x + p.div_wall + p.right_w / 2.0,
            p.outer_y - (p.wall + 2.0) / 2.0,
            p.usbc_center_z - p.usbc_h / 2.0,
            p.usbc_w,
            p.wall + 2.0,
            p.usbc_h,
        )
    )
    cuts.append(
        box_from_z(
            p.div_x + p.div_wall / 2.0,
            p.outer_y / 2.0,
            p.cable_slot_z,
            p.div_wall + 1.0,
            p.cable_slot_w,
            p.cable_slot_h,
        )
    )

    vent_total = p.vent_w + (p.vent_count - 1) * p.vent_spacing
    vent_x0 = p.right_zone_x0 + (p.right_w - vent_total) / 2.0 + p.vent_w / 2.0
    vent_y0 = (p.outer_y - p.vent_l) / 2.0 + p.vent_l / 2.0
    for i in range(p.vent_count):
        cuts.append(
            box_from_z(
                vent_x0 + i * p.vent_spacing,
                vent_y0,
                -0.2,
                p.vent_w,
                p.vent_l,
                p.floor_th + 0.4,
            )
        )

    for x, y in p.all_screw_positions():
        cuts.append(cylinder_from_z(x, y, -0.5, 200.0, p.screw_tap_d))

    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            x = p.js_center_x + sx * p.js_mount_pitch_x / 2.0
            y = p.js_center_y + sy * p.js_mount_pitch_y / 2.0
            cuts.append(cylinder_from_z(x, y, p.floor_th - 0.2, p.js_support_h + 0.4, p.js_support_tap_d))

    return cut_all(base, cuts)


def lid_tongue(
    x0: float,
    width: float,
    top_fn,
    p: EnclosureParams,
) -> cq.Workplane:
    y0 = p.wall + p.lid_fit_gap
    depth = p.outer_y - 2.0 * (p.wall + p.lid_fit_gap)
    y1 = y0 + depth

    outer = sloped_slab(
        x0 + p.lid_fit_gap,
        width - 2.0 * p.lid_fit_gap,
        y0,
        depth,
        top_fn(y0),
        top_fn(y1),
        p.lid_tongue_depth,
    )
    inner = sloped_slab(
        x0 + p.lid_fit_gap + p.lid_tongue_wall,
        width - 2.0 * (p.lid_fit_gap + p.lid_tongue_wall),
        y0 + p.lid_tongue_wall,
        depth - 2.0 * p.lid_tongue_wall,
        top_fn(y0 + p.lid_tongue_wall),
        top_fn(y1 - p.lid_tongue_wall),
        p.lid_tongue_depth,
    )
    return outer.cut(inner)


def build_left_lid(p: EnclosureParams) -> cq.Workplane:
    lid = sloped_slab(0.0, p.left_lid_width, 0.0, p.outer_y, p.left_front_h, p.left_back_h, p.lid_th)
    tongue = lid_tongue(p.wall, p.left_w, p.left_under, p)
    lid = lid.union(tongue)

    js_under_d, js_top_d = joystick_aperture_diameters(p)
    js_rear_y = p.js_center_y + max(js_top_d, p.js_face_relief_d) / 2.0
    js_front_y = p.js_center_y - max(js_top_d, p.js_face_relief_d) / 2.0
    js_top_cut_z = p.left_top(js_rear_y) + 0.6
    js_relief_z0 = p.left_top(js_front_y) - p.js_face_relief_depth
    js_through_d = joystick_aperture_diameter_at_z(p, js_top_cut_z) + p.js_through_margin
    cuts: list[cq.Workplane] = []
    cuts.append(
        cylinder_from_z(
            p.js_center_x,
            p.js_center_y,
            js_relief_z0,
            js_top_cut_z - js_relief_z0,
            p.js_face_relief_d,
        )
    )
    cuts.append(
        cylinder_from_z(
            p.js_center_x,
            p.js_center_y,
            -1.0,
            js_top_cut_z + 1.0,
            js_through_d,
        )
    )
    cuts.append(
        sloped_block_centered(
            p.oled_window_center_x,
            p.oled_window_center_y,
            p.oled_bezel_x,
            p.oled_bezel_y,
            p.left_top,
            p.oled_bezel_depth,
        )
    )
    cuts.append(
        sloped_block_centered(
            p.oled_center_x,
            p.oled_center_y,
            p.oled_pocket_x,
            p.oled_pocket_y,
            lambda y: p.left_under(y) + p.oled_pocket_depth,
            p.oled_pocket_depth + 0.4,
        )
    )
    cuts.append(
        rounded_rect_prism(
            p.oled_window_center_x,
            p.oled_window_center_y,
            -1.0,
            p.oled_window_x,
            p.oled_window_y,
            p.oled_window_r,
            200.0,
        )
    )
    cuts.append(
        sloped_block_centered(
            p.oled_center_x,
            p.oled_center_y + p.oled_board_y / 2.0 + p.oled_wire_notch_y / 2.0 - 1.2,
            p.oled_wire_notch_x,
            p.oled_wire_notch_y,
            lambda y: p.left_under(y) + p.oled_pocket_depth,
            p.oled_pocket_depth + 0.4,
        )
    )
    for x, y in p.oled_hole_positions():
        cuts.append(
            cylinder_from_z(
                x,
                y,
                -1.0,
                200.0,
                p.oled_screw_clear_d,
            )
        )
        cuts.append(
            cylinder_from_z(
                x,
                y,
                p.left_top(y) - p.oled_screw_head_depth,
                p.oled_screw_head_depth + 0.4,
                p.oled_screw_head_d,
            )
        )
    for x, y in p.left_screw_positions():
        cuts.append(cylinder_from_z(x, y, -1.0, 200.0, p.screw_clear_d))
        cuts.append(cylinder_from_z(x, y, p.left_top(y) - p.screw_head_depth, p.screw_head_depth + 0.6, p.screw_head_d))

    return cut_all(lid, cuts)


def build_right_lid(p: EnclosureParams) -> cq.Workplane:
    lid = sloped_slab(
        p.right_lid_x0,
        p.right_lid_width,
        0.0,
        p.outer_y,
        p.right_front_h,
        p.right_back_h,
        p.lid_th,
    )
    tongue = lid_tongue(p.right_zone_x0, p.right_w, p.right_under, p)
    lid = lid.union(tongue)

    cuts: list[cq.Workplane] = []
    cuts.append(
        rounded_rect_prism(
            p.kp_center_x,
            p.kp_center_y,
            p.right_top(p.kp_center_y) - p.kp_recess_depth,
            p.kp_outer_x,
            p.kp_outer_y,
            p.kp_outer_r,
            p.kp_recess_depth + 0.4,
        )
    )
    cuts.append(
        rounded_rect_prism(
            p.kp_center_x,
            p.kp_center_y,
            -1.0,
            p.kp_body_cut_x,
            p.kp_body_cut_y,
            p.kp_body_cut_r,
            200.0,
        )
    )
    cuts.append(
        box_from_z(
            p.kp_center_x,
            p.kp_center_y + p.kp_outer_y / 2.0 - p.kp_tail_slot_back_offset - p.kp_tail_slot_y / 2.0,
            -1.0,
            p.kp_tail_slot_x,
            p.kp_tail_slot_y,
            200.0,
        )
    )
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            x = p.kp_center_x + sx * p.kp_mount_pitch_x / 2.0
            y = p.kp_center_y + sy * p.kp_mount_pitch_y / 2.0
            cuts.append(cylinder_from_z(x, y, -1.0, 200.0, p.kp_mount_hole_d))
            cuts.append(
                cylinder_from_z(
                    x,
                    y,
                    p.right_top(y) - p.kp_mount_counterbore_depth,
                    p.kp_mount_counterbore_depth + 0.4,
                    p.kp_mount_counterbore_d,
                )
            )
    for x, y in p.right_screw_positions():
        cuts.append(cylinder_from_z(x, y, -1.0, 200.0, p.screw_clear_d))
        cuts.append(cylinder_from_z(x, y, p.right_top(y) - p.screw_head_depth, p.screw_head_depth + 0.6, p.screw_head_d))

    return cut_all(lid, cuts)


def build_models(p: EnclosureParams | None = None) -> dict[str, cq.Workplane]:
    params = p or EnclosureParams()
    return {
        "base": build_base(params),
        "lid_left": build_left_lid(params),
        "lid_right": build_right_lid(params),
    }


def export_models(models: dict[str, cq.Workplane], outdir: Path, fmt: str) -> None:
    outdir.mkdir(parents=True, exist_ok=True)
    for name, model in models.items():
        if fmt in {"step", "both"}:
            exporters.export(model, str(outdir / f"{name}.step"))
        if fmt in {"stl", "both"}:
            exporters.export(model, str(outdir / f"{name}.stl"))


def show_models(models: dict[str, cq.Workplane], port: int | None = None) -> None:
    from ocp_vscode import show

    show(
        *models.values(),
        names=list(models.keys()),
        axes=True,
        grid=True,
        collapse=1,
        port=port,
    )


def print_model_bounds(models: dict[str, cq.Workplane]) -> None:
    for name, model in models.items():
        bb = model.val().BoundingBox()
        print(f"{name}: x={bb.xlen:.2f} y={bb.ylen:.2f} z={bb.zlen:.2f}")


def run_once(args: argparse.Namespace) -> int:
    models = build_models()
    if args.part != "all":
        models = {args.part: models[args.part]}

    if args.format != "none":
        export_models(models, Path(args.outdir), args.format)

    if args.viewer:
        show_models(models, port=args.viewer_port)

    print_model_bounds(models)
    return 0


def _snapshot_mtimes(paths: list[Path]) -> dict[Path, int | None]:
    mtimes: dict[Path, int | None] = {}
    for path in paths:
        try:
            mtimes[path] = path.stat().st_mtime_ns
        except FileNotFoundError:
            mtimes[path] = None
    return mtimes


def watch_loop(raw_argv: list[str], poll_interval: float) -> int:
    script_path = Path(__file__).resolve()
    watch_paths = [script_path]
    child_argv: list[str] = []

    skip_next = False
    for i, arg in enumerate(raw_argv):
        if skip_next:
            skip_next = False
            continue
        if arg == "--watch":
            continue
        if arg.startswith("--watch-poll="):
            continue
        if arg == "--watch-poll":
            skip_next = True
            continue
        child_argv.append(arg)

    snapshot = _snapshot_mtimes(watch_paths)
    print(f"watching {script_path} (poll={poll_interval:.2f}s)", flush=True)
    print("press Ctrl-C to stop", flush=True)

    while True:
        result = subprocess.run([sys.executable, str(script_path), *child_argv], check=False)
        try:
            while True:
                time.sleep(poll_interval)
                current = _snapshot_mtimes(watch_paths)
                if current != snapshot:
                    snapshot = current
                    print("\nchange detected, rerunning...\n", flush=True)
                    break
        except KeyboardInterrupt:
            return result.returncode


def main() -> None:
    parser = argparse.ArgumentParser(description="CadQuery enclosure model for o3Chords.")
    parser.add_argument("--part", choices=["base", "lid_left", "lid_right", "all"], default="all")
    parser.add_argument("--format", choices=["none", "step", "stl", "both"], default="none")
    parser.add_argument("--outdir", default=str(Path(__file__).with_name("stl")))
    parser.add_argument("--viewer", action="store_true", help="Send the model to OCP CAD Viewer.")
    parser.add_argument("--viewer-port", type=int, default=None, help="Optional OCP CAD Viewer port.")
    parser.add_argument("--watch", action="store_true", help="Rerun the script when this file changes.")
    parser.add_argument("--watch-poll", type=float, default=0.75, help="Watch polling interval in seconds.")
    args = parser.parse_args()

    if args.watch:
        raise SystemExit(watch_loop(sys.argv[1:], args.watch_poll))

    raise SystemExit(run_once(args))


if __name__ == "__main__":
    main()
