// Dummy wall plate for the Guition ESP32-4848S040 display-only module.
// Screws to a European round box (60 mm screw spacing). The display then
// sits on four pins. Each pin is 4.4 mm tall under an outward hook that
// locks into a hole in the frame, 1.4 mm high and 1.4 mm deep.
// USB-C and the SD slot face down.
//
// Pin size, pitch, and hook follow enclosure/Guition_4848s040_EU_plate.stl.
// 65.6 mm is the pitch of the two pins on one side (centre to centre).
// 82.1 mm is outside face to outside face of the two opposite rows.
// The rows sit on the left and right. Set pin_rotation to 90 if the slots
// are on the top and bottom instead.
//
// The left screw slot runs sideways. The right one runs up and down, so
// the plate can be shifted and squared before the screws are tightened.
//
// Print the gauge first. It has the same outline and holes as the plate,
// so the pin fit and the hole positions can both be checked. PETG, 0.2 mm layers,
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
// Printed pin section, taken from the EU plate. pin_clear shrinks it.
pin_w = 3.6;
pin_t = 2.5;
pin_clear = 0;
// Pin height from the plate up to the underside of the hook.
stem_h = 4.4;
// Hook that locks into the frame.
hole_h = 1.4;
hole_d = 1.4;
// Centre of one pin to the centre of the pin beside it.
pin_pitch = 65.6;
// Outside face of one row to the outside face of the opposite row.
pin_back = 82.1;
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
cable_w = 12;

/* [Plate] */
plate_t = 3.6;
// Gauge only. Thin so a fit check prints quickly.
gauge_t = 1.2;
corner_r = 3.5;

/* [Header] */
// From the center of the display, looking at the back, USB edge down.
// The red block in the preview is this socket. Its hole is this size
// plus header_clear on every side.
header_x = -14;
header_y = 28;
header_w = 20;
header_h = 8;
header_t = 9;
header_clear = 1;

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

module screw_cut(span, diameter, depth, z0, vertical = false) {
    hull() {
        translate(vertical ? [0, -span, z0] : [-span, 0, z0])
            cylinder(d = diameter, h = depth);
        translate(vertical ? [0, span, z0] : [span, 0, z0])
            cylinder(d = diameter, h = depth);
    }
}

// Windows and screw slots, in the same places on the plate and the gauge.
// The large window is centered on the plate. The header sits above that
// window and has its own hole.
module plate_holes(depth) {
    plate_center = -display_drop;
    plate_bottom = plate_center - display_h / 2;
    opening_bottom = plate_center - opening_h / 2;

    translate([0, plate_center, -1])
        rounded_slab(opening_w, opening_h, depth, 3);

    translate([
        header_x,
        header_y + plate_center,
        -1
    ])
        cube([
            header_w + 2 * header_clear,
            header_h + 2 * header_clear,
            depth
        ], center = true);

    translate([0, (plate_bottom + opening_bottom) / 2, -1])
        cube([
            cable_w,
            opening_bottom - plate_bottom + 0.8,
            depth
        ], center = true);

    // Left slot is horizontal. Right slot is vertical.
    for (sx = [-1, 1])
        translate([sx * screw_span / 2, 0, -1])
            screw_cut(slot_extra, screw_d, depth, 0, sx > 0);
}

module backplate() {
    difference() {
        translate([0, -display_drop, 0])
            rounded_slab(display_w, display_h, plate_t, corner_r);

        plate_holes(plate_t + 2);

        for (sx = [-1, 1])
            translate([sx * screw_span / 2, 0, plate_t - screw_head_h])
                screw_cut(slot_extra, screw_head_d, screw_head_h + 0.2, 0, sx > 0);
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

// Thin copy of the plate, with the same holes, for a fit and placement check.
module gauge() {
    difference() {
        translate([0, -display_drop, 0])
            rounded_slab(display_w, display_h, gauge_t, corner_r);
        plate_holes(gauge_t + 2);
    }
    translate([0, -display_drop, 0])
        rotate([0, 0, pin_rotation])
            attachment_pins(gauge_t);
}

if (part == "gauge")
    gauge();
else
    backplate();
