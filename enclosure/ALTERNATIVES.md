# Other backplates for the Guition 4848S040

The plate in [`backplate.scad`](backplate.scad) screws to a European round box (60 mm screw spacing). Four outward hook pins lock into the display frame. USB-C and the SD slot face down, and the display sits 8 mm below the screw line so the 8-pin header hangs into the wall box. The prints below solve that same mounting problem.

Links for every model are also listed in [`references/README.md`](references/README.md). The STL files are not stored in this repository. Printables and MakerWorld do not offer a clearly unauthenticated download.

## Snap or click onto the display

| What | Type | Fit | Link |
| --- | --- | --- | --- |
| OEM Guition relay base | Product | The front clicks onto a plastic wall base. The relay variant is about €30. People on the Home Assistant forum keep the empty shell and set the relay board aside. | Guition store on AliExpress, listings “with 1 or 3 relays” |
| MakerWorld wall backplate | STL | The display clips on and comes off again. A pass-through is there for a 5 V cable. The model says to remove the display’s rear cover. | [MakerWorld 2801803](https://makerworld.com/en/models/2801803-guition-esp32s3-4848s040-wall-backplate) |
| Kh plate, scottbez1 remix | STL | Hook-style mount. The remix adds holes so Command strips can be reached. | [Printables 1327763](https://www.printables.com/model/1327763-guition-4-esp32-s3-lvgl-with-command-adhesive-stri), remix of the [Kh Guition 4″ plate](https://www.printables.com/model/1257111-guition-4-esp32-s3-lvgl) |
| Totof60 magnet wall mount | STL | Remix of the Kh plate. Magnets sit in the display recesses. The author dropped the pins because they were too fragile. | [Printables 1736105](https://www.printables.com/model/1736105-wall-mount-magnet-guition-4) |

## European wall box

| What | Type | Notes | Link |
| --- | --- | --- | --- |
| Robertoooo EU wall mount | STL | Same arrangement as this SCAD file: the no-relay module, a 60 mm box, a downward shift for the 8-pin header, USB and SD facing down. | [Printables 1440541](https://www.printables.com/model/1440541-guition-esp32-s3-4848s040-eu-wall-mount) |
| Protoncek “LCD dose” | STL | An in-wall box. Screw holes are drilled, and the box uses heat-set inserts. | [Printables 1229100](https://www.printables.com/model/1229100-guition-esp32-4848s040-lcd-dose) |

## Other cases

These are enclosures or stands. They do not snap onto the display’s own hook slots.

- [Jager magnet case](https://www.printables.com/model/1766973-case-for-guition-4848s040-4) — two parts, 6×3 mm magnets, and an adapter for a metal surface.
- [Sarah Matthies wallmount case](https://www.printables.com/model/1654839-guiton-4-480x480-esp32-s3-4848s040-smart-display-c) — enclosure with room for a battery, USB left reachable.
- [juliannesermon desk stand](https://www.thingiverse.com/thing:6666567) and [cruftbox desk stand](https://www.thingiverse.com/thing:7373058) — stands, not wall plates. [maximalimage’s case](https://www.thingiverse.com/thing:7319929) is a general enclosure.

Mounting is also discussed on the [Home Assistant Guition 4″ thread](https://community.home-assistant.io/t/guition-4-480x480-esp32-s3-4848s040-smart-display-with-lvgl/729271).

## What to use

- For the factory click-fit, buy the relay-base Guition and keep the empty plastic back. The relay board can be set aside, or the base can stay as the power supply.
- For a printed snap plate, start with MakerWorld 2801803 or the Kh / scottbez1 family.
- For a printed plate on a European box, Robertoooo 1440541 is the nearest published design. This SCAD file is in that same class: a parametric plate plus a thin gauge.
- For a box with depth, use the Protoncek dose, or the Jager or Sarah cases.
