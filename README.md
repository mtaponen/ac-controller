# AC panel

Wall panel for a Mitsubishi Electric air-to-air heat pump. It runs on a Guition ESP32-S3 board with a 4 inch 480×480 touchscreen (ESP32-4848S040) and talks to the unit through MelCloud, the same cloud service as the phone app and the Home Assistant integration.

The panel shows the current mode, fan, setpoint, and room temperature. A change sends only that field, so a temperature tap does not put back an older mode or fan from another controller. The temperature ring and the + / − buttons run from 12 °C to 28 °C. In Cool and Auto the heat pump’s own minimum still applies when it is higher than 12 °C.

There is no Home Assistant API, web server, or over-the-air update on the device. The Wi-Fi name and the MelCloud account are compiled into the firmware from `secrets.yaml`.

## One-time setup

Use Python 3.13. ESPHome 2026.4.0 and 2026.4.1 boot-loop this board; this project uses 2026.10.1.

```sh
python3.13 -m venv .venv
.venv/bin/pip install 'esphome==2026.10.1'
cp secrets.yaml.example secrets.yaml
```

Edit `secrets.yaml`. If the MelCloud password contains `#`, keep it in double quotes. `secrets.yaml` is gitignored.

Program the board through the CH340 serial adapter, not the native USB port. After plugging it in, the port is `/dev/cu.usbserial-…` and the number can change.

## Rebuild and flash

`secrets.yaml` and `panel.yaml` are baked in at compile time. A change takes effect on the next flash.

```sh
.venv/bin/esphome run panel.yaml --device /dev/cu.usbserial-310
```

Replace the device path with the port from `ls /dev/cu.usbserial*`. The command compiles, uploads, and then prints the serial log. Stop the log with Ctrl-C. To upload a binary that is already built:

```sh
.venv/bin/esphome upload panel.yaml --device /dev/cu.usbserial-310
```

## Display driver

`components/mipi_rgb` is a local copy of ESPHome’s RGB panel driver. It keeps a larger slice of the picture in internal memory and does not restart the video stream on every refresh, which stops the screen flashing when Wi-Fi uses the same memory bus. After an ESPHome upgrade, compare that directory with the new package and keep those two changes.
