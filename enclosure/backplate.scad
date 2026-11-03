// Dummy wall plate for the Guition ESP32-4848S040 display-only module.
// Screws to a European round box (60 mm screw spacing). The display then
// sits on four pins. Each pin is 2.5 mm tall under an outward hook that
// locks into a hole in the frame, 2 mm high and 1.5 mm deep.
// USB-C and the SD slot face down.
//
// Measured slots: 4.2 mm wide, 2.5 mm thick at most.
// 61.3 mm is the pitch of the two pins on one side (same edge to same edge).
// 79 mm is back face to back face of the two opposite rows.
// The rows sit on the left and right. Set pin_rotation to 90 if the slots
// are on the top and bottom instead.
//
// Print the gauge first and try it in the frame. PETG, 0.2 mm layers,
// 0.4 mm nozzle, 4 perimeters. No supports. Back of the plate on the bed,
// pins pointing up.
//
// OpenSCAD: open this file, turn on the customizer, render, export STL.

/* [Part] */
part = "plate"; // [plate, gauge]

/* [Display] */
display_w = 86.5;
display_h = 86.5;
// Guition drawing marks this "T". The plate sits behind the housing.
display_thickness = 13.6;
// Moves the display down relative to the screws, so the 8-pin header
// near the top of the board hangs into the box.
display_drop = 8;

/* [Pins] */
// Slot size. The printed pin is this minus pin_clear so it can enter.
pin_w = 4.2;
pin_t = 2.5;
pin_clear = 0.2;
// Pin height from the plate up to the underside of the hook.
stem_h = 2.5;
// Hole the hook locks into.
hole_h = 2;
hole_d = 1.5;
// Same edge of one pin to the same edge of the pin beside it.
pin_pitch = 61.3;
// Outside of one row to the outside of the opposite row.
pin_back = 79;
// 0: pins on the left and right. 90: pins on the top and bottom.
pin_rotation = 0; // [0, 90]

/* [Wall box] */
screw_span = 60;
screw_d = 4.2;
slot_extra = 3.2;
screw_head_d = 8;
screw_head_h = 1.6;

/* [Openings] */
opening_w = 36;
opening_h = 68;
upper_w = 72;
upper_y0 = 11;
upper_y1 = 32;
cable_w = 12;

/* [Plate] */
plate_t = 3.6;
// Gauge only. Thin so a fit check prints quickly.
gauge_t = 1.2;
corner_r = 3.5;

/* [Header] */
// From the center of the display, looking at the back, USB edge down.
// The red block in the preview is this socket. Keep it in the hole.
header_x = -14;
header_y = 28;
header_w = 20;
header_h = 8;
header_t = 9;

/* [Hidden] */
$fn = 64;

module rounded_slab(w, h, t, r) {
    linear_extrude(t, convexity = 4)
        offset(r = r)
            offset(delta = -r)
                square([w, h], center = true);
}

// Local axes: +Z toward the device, +Y outward, width along X.
// The inner face is one straight wall. The hook is only on the outside:
// a level ledge, then a top that slopes back to the pin.
//
//        /|              |\
//        ||              ||
//   =========================   <- plate, wall is below
module attachment_pin() {
    w = pin_w - pin_clear;
    t = pin_t - pin_clear;
    hh = hole_h - pin_clear;
    hd = hole_d - pin_clear;
    z0 = stem_h;

    translate([-w / 2, -t, -0.2])
        cube([w, t, stem_h + hh + 0.2]);

    // polygon x = outward, polygon y = height. Extrude along the width.
    translate([-w / 2, 0, 0])
        rotate([0, 0, 90])
            rotate([90, 0, 0])
                linear_extrude(w, convexity = 4)
                    polygon([
                        [-0.2, z0],
                        [hd, z0],
                        [-0.2, z0 + hh]
                    ]);
}

module attachment_pins(base_t) {
    for (sy = [-1, 1]) {
        translate([pin_back / 2, sy * pin_pitch / 2, base_t])
            rotate([0, 0, -90])
                attachment_pin();
        translate([-pin_back / 2, sy * pin_pitch / 2, base_t])
            rotate([0, 0, 90])
                attachment_pin();
    }
}

module screw_cut(span, diameter, depth, z0) {
    hull() {
        translate([-span, 0, z0])
            cylinder(d = diameter, h = depth);
        translate([span, 0, z0])
            cylinder(d = diameter, h = depth);
    }
}

module backplate() {
    plate_bottom = -display_drop - display_h / 2;

    difference() {
        translate([0, -display_drop, 0])
            rounded_slab(display_w, display_h, plate_t, corner_r);

        translate([0, 0, -1])
            rounded_slab(opening_w, opening_h, plate_t + 2, 3);

        translate([0, (upper_y0 + upper_y1) / 2, -1])
            cube([upper_w, upper_y1 - upper_y0, plate_t + 2], center = true);

        translate([0, (plate_bottom - opening_h / 2) / 2, -1])
            cube([
                cable_w,
                -plate_bottom - opening_h / 2 + 0.8,
                plate_t + 2
            ], center = true);

        for (sx = [-1, 1]) {
            translate([sx * screw_span / 2, 0, -1])
                screw_cut(slot_extra, screw_d, plate_t + 2, 0);
            translate([sx * screw_span / 2, 0, plate_t - screw_head_h])
                screw_cut(slot_extra, screw_head_d, screw_head_h + 0.2, 0);
        }
    }

    translate([0, -display_drop, 0])
        rotate([0, 0, pin_rotation])
            attachment_pins(plate_t);

    %translate([0, -display_drop, plate_t + display_thickness / 2])
        cube([display_w, display_h, display_thickness], center = true);
    %translate([header_x, header_y - display_drop, plate_t + header_t / 2])
        color("crimson")
            cube([header_w, header_h, header_t], center = true);
}

// Same four pins, on a thin pad, for a quick measurement check.
module gauge() {
    gauge_h = pin_pitch + pin_w + 10;
    rounded_slab(pin_back, gauge_h, gauge_t, 2);
    rotate([0, 0, pin_rotation])
        attachment_pins(gauge_t);
}

if (part == "gauge")
    gauge();
else
    backplate();
