# keybm

Keyboard and mouse emulator for the M5Stack Cardputer ADV. Works over USB or Bluetooth LE, with three Bluetooth slots, Unit Scroll support, and IMU tilt control.

## Link

- <b>Bluetooth</b> is the default. On boot the firmware waits briefly (`usb_wait`, 150 ms by default) for a USB host to start talking. If one does, the session runs over USB; chargers and power banks fall through to Bluetooth.
- <b>Forcing a link:</b> set `link=bt` or `link=usb` in the config to skip the check.

## Modes

The device boots into mouse mode. <b>G0</b> toggles between mouse and keyboard on the active link or slot.

### Mouse

| Key | Action |
|---|---|
| `;` `.` `,` `/` | move up / down / left / right |
| `ok` | left button |
| `\` | right button |
| `'` | middle button |
| `=` / `]` | wheel up / down |
| `[` / `-` | button 4 (back) / button 5 (forward) |
| `i` | IMU tilt control on / off |
| `1` `2` `3` | Bluetooth slot (saves and restarts) |
| `m` / `n` | brighter / dimmer |

IMU control reads roll for horizontal movement and pitch for vertical movement. The gauge dot shows cursor direction and speed. While off, the IMU sleeps and is not polled.

### Keyboard

Standard Cardputer keys. `fn` plus `;` `.` `,` `/` sends arrow keys, `fn` plus `1` to `9` sends F1 to F9, `fn` plus `0` `-` `=` sends F10 F11 F12, and `opt` sends Win/Cmd. The screen shows the key map with held keys lit.

### Unit Scroll

With the unit on the Grove port, the wheel scrolls in mouse mode and sends Page Up / Page Down in keyboard mode.

## Bluetooth slots

Each slot advertises under its own address as `keybm 1`, `keybm 2` and `keybm 3`, so each host pairs to one slot only. Holding <b>G0</b> for 3 s clears the active slot's pairing and restarts; remove the device on that host too.

The slot-to-host map is kept in `/.keybm/bonds`.

## Config

`/.keybm/config` is created with defaults on first boot. Changes take effect after a restart.

| Key | Default | Meaning |
|---|---|---|
| `theme` | `F88C00` | line and highlight colour |
| `imu_zero` | `level` | `level` = gravity, `pose` = orientation when `i` is pressed |
| `imu_dead` | `3` | deadzone in degrees |
| `imu_max` | `30` | tilt in degrees that reaches full speed |
| `imu_speed` | `12` | counts per report at full tilt |
| `imu_axes` | `-x-y` | sensor axis and sign driving cursor X then Y |
| `move_speed` | `2` | arrow-key counts per report |
| `brightness` | `128` | saved after `m` / `n` |
| `bright_step` | `8` | brightness change per press |
| `slot` | `1` | active Bluetooth slot |
| `link` | `auto` | `auto`, `bt` or `usb` |
| `usb_wait` | `150` | USB host detection window in ms |
| `name` | `keybm` | Bluetooth name, slot number appended |
| `scroll_dir` | `1` | `-1` reverses the Unit Scroll |

## Difference from fork

- USB or Bluetooth chosen automatically instead of a selection screen
- Three Bluetooth slots, with slot-scoped unpair
- Buttons 4 and 5, wheel keys, IMU tilt control
- Unit Scroll support
- Brightness on `m` / `n` in finer steps
- F1 to F12 on `fn` plus the number row
- New display: key map in keyboard mode, IMU gauge and mouse graphic in mouse mode
- Reports sent only on change, independent mouse buttons, matched cursor speed across USB and Bluetooth
- Library versions pinned
