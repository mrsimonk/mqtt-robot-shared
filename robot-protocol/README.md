# Robot Protocol

This component defines the **JSON command protocol** used to control the robot. It is transport‑agnostic: the same JSON documents can be sent over MQTT, serial, WebSocket, etc. The `protocol` module is responsible for parsing these JSON documents and dispatching them to user‑supplied handlers.

The public API for consumers of this component is defined in `include/protocol.h`.

---

## Top‑level message structure

Every message processed by `protocol_handle_command_json` must be a single JSON object with a required `type` field.

```jsonc
{
  "type": "command" | "sequence" | "config",
  // other fields depend on type
}
```

- **`type`** (string, required)
  - `"command"` – a single command (e.g. drive, turn, stop, led, etc.).
  - `"sequence"` – an ordered list of commands to execute in sequence.
  - `"config"` – drive configuration / calibration data.

If `type` is missing or not a string, the message is ignored and a warning is logged.

---

## Type: `"command"`

For `type == "command"`, the message must contain a `command` object:

```jsonc
{
  "type": "command",
  "command": {
    "kind": "drive" | "turn" | "led_hsv" | "immediate" |
             "stop" | "wait" | "pause" | "resume" | "clear_queue",
    // other fields depend on kind
  }
}
```

- **`command`** (object, required)
  - Parsed by `handle_single_command_object()`.
  - Must contain a string field `kind`.
- **`kind`** (string, required)
  - Dispatches to a specific handler based on its value (see below).
  - Unknown values are logged as a warning and the message is rejected.

### `kind: "drive"`

Issues a straight‑line drive command.

```jsonc
{
  "type": "command",
  "command": {
    "kind": "drive",
    "direction": "forward" | "backward", // required
    "speed": 100,                          // mm/s, required
    "duration": 2000,                      // ms, optional
    "distance": 500                        // mm, optional
  }
}
```

Fields:

- **`direction`** (string, required)
  - Passed directly to the user handler `drive(direction, ...)`.
  - **Recommended valid values:** `"forward"`, `"backward"`.
  - At the drive layer, this is typically mapped to `DRIVE_DIRECTION_FORWARD` / `DRIVE_DIRECTION_BACKWARD`.
- **`speed`** (number, required)
  - Interpreted as integer millimetres per second.
  - Converted to `int32_t speed_mm_per_s`.
- **`duration`** (number, optional)
  - Milliseconds.
  - If missing or not a number, treated as `0`.
- **`distance`** (number, optional)
  - Millimetres.
  - If missing or not a number, treated as `0`.

Behaviour:

- If `direction` is not a string or `speed` is not numeric, the command is rejected.
- If a `drive` handler is installed in `protocol_handlers_t`, it is called as:
  - `drive(direction, speed_mm_per_s, duration_ms, distance_mm)`.

Constraints / recommendations:

- The protocol layer does **not** clamp `speed`, `duration`, or `distance`, but the drive module typically applies:
  - `speed_mm_per_s >= 0`.
  - An upper limit based on configuration `max_speed_mm_per_s` (see `config` section).
- If both `distance` and a positive `speed` are provided, the drive layer derives `duration` as `distance / speed`.

### `kind: "turn"`

Issues a turn along a circular arc.

```jsonc
{
  "type": "command",
  "command": {
    "kind": "turn",
    "radius": 200,     // mm, required
    "angle": 90,       // degrees, required (sign indicates direction)
    "speed": 150,      // mm/s, optional
    "duration": 1000   // ms, optional
  }
}
```

Fields:

- **`radius`** (number, required)
  - Radius of the turn in millimetres.
  - Converted to `int32_t radius_mm`.
- **`angle`** (number, required)
  - Angle in degrees.
  - Converted to `int32_t angle_deg`.
- **`speed`** (number, optional)
  - Linear speed in mm/s.
  - Converted to `int32_t speed_mm_per_s`.
  - Defaults to `0` if missing or not numeric.
- **`duration`** (number, optional)
  - Duration in milliseconds.
  - Converted to `uint32_t duration_ms`.
  - Defaults to `0` if missing or not numeric.

Constraints / behaviour:

- If `radius` or `angle` is not numeric, the command is rejected.
- At least **one of** `speed` or `duration` must be supplied:
  - If `speed_mm_per_s <= 0` **and** `duration_ms == 0`, the command is rejected.
- If a `turn` handler is installed, it is called as:
  - `turn(radius_mm, angle_deg, speed_mm_per_s, duration_ms)`.

Additional constraints / recommendations:

- `radius_mm` may be application‑defined; the drive layer allows `radius_mm == 0` to mean an on‑the‑spot turn.
- When duration is provided but speed is not, the drive code derives `speed_mm_per_s` from the requested `angle`, `radius`, and `duration`, and clamps it to the configured `max_speed_mm_per_s` if necessary.
- When speed is provided but duration is not, the drive code derives `duration_ms` from geometry (arc length / speed).

### `kind: "led_hsv"`

Sets an LED color in HSV space.

```jsonc
{
  "type": "command",
  "command": {
    "kind": "led_hsv",
    "h": 120,   // required
    "s": 255,   // optional, default 255
    "v": 32     // optional, default 32
  }
}
```

Fields:

- **`h`** (number, required)
  - Hue; converted to `uint16_t`.
- **`s`** (number, optional)
  - Saturation; converted to `uint8_t`.
  - Defaults to `255` if missing or not numeric.
- **`v`** (number, optional)
  - Value/brightness; converted to `uint8_t`.
  - Defaults to `32` if missing or not numeric.

Behaviour:

- If `h` is not numeric, the command is rejected.
- If a `set_led_hsv` handler is installed, it is called as:
  - `set_led_hsv(hue, sat, val)`.

### `kind: "immediate"`

Direct, low‑level control of the left and right motors as fractional outputs. Intended for joystick‑style control.

```jsonc
{
  "type": "command",
  "command": {
    "kind": "immediate",
    "left": -0.5,       // required
    "right": 0.5,       // required
    "timeout_ms": 200,  // optional, default 200
    "now_ms": 123456    // optional, ignored by parser but can be present
  }
}
```

Fields:

- **`left`** (number, required)
  - Fractional output for the left side, typically in range [-1.0, 1.0].
  - Parsed as `float left_frac`.
- **`right`** (number, required)
  - Fractional output for the right side.
  - Parsed as `float right_frac`.
- **`timeout_ms`** (number, optional)
  - Maximum duration to apply the command, in milliseconds.
  - If missing or not numeric, defaults to `200`.
- **`now_ms`** (number, optional)
  - **Note:** The parser does not read `now_ms` from JSON.
  - Instead it calls `esp_log_timestamp()` to compute `now_ms` internally.

Behaviour:

- If `left` or `right` is missing or not numeric, the command is rejected.
- `timeout_ms` defaults to `200` if not supplied.
- `now_ms` is set from the current log timestamp.
- If an `immediate` handler is installed, it is called as:
  - `immediate(left_frac, right_frac, timeout_ms, now_ms)`.

Additional constraints / recommendations:

- The drive module is designed for `left_frac` and `right_frac` in the closed interval **[-1.0, 1.0]**.
- A small deadband of about **±0.02** is applied: values whose magnitude is below this are treated as `0.0`.

#### Generating an `immediate` command from C

The helper `protocol_generate_immediate_command()` formats a JSON document that matches the `"immediate"` command format above:

```c
void protocol_generate_immediate_command(char *buffer,
                                         size_t buffer_size,
                                         float left_frac,
                                         float right_frac,
                                         uint32_t timeout_ms,
                                         uint32_t now_ms);
```

- Writes a null‑terminated JSON string into `buffer`.
- Example output (whitespace removed):

```jsonc
{"type":"command",
 "command":{
   "kind":"immediate",
   "left":-0.100,
   "right":0.300,
   "timeout_ms":200,
   "now_ms":123456
 }}
```

The generated JSON is suitable for sending back over the transport you are using (e.g. MQTT).

### `kind: "stop"`

Stops the robot.

```jsonc
{
  "type": "command",
  "command": {
    "kind": "stop"
  }
}
```

Behaviour:

- If a `stop` handler is installed, it is called with no arguments.

### `kind: "wait"`

Represents a time delay within a sequence or queue.

```jsonc
{
  "type": "command",
  "command": {
    "kind": "wait",
    "duration": 1000 // ms (convention; currently not interpreted here)
  }
}
```

Behaviour:

- Currently this is a **no‑op in the protocol module**; it simply returns `true`.
- Higher‑level code is expected to implement the actual waiting behaviour if desired.

### `kind: "pause"`

Pauses the current command / queue.

```jsonc
{
  "type": "command",
  "command": { "kind": "pause" }
}
```

Behaviour:

- Currently a stub in the protocol module: it returns `true` without calling any handler.
- Higher‑level code can extend this to integrate with a motion queue.

### `kind: "resume"`

Resumes a paused command / queue.

```jsonc
{
  "type": "command",
  "command": { "kind": "resume" }
}
```

Behaviour:

- Currently a stub in the protocol module: it returns `true` without calling any handler.

### `kind: "clear_queue"`

Clears any queued commands.

```jsonc
{
  "type": "command",
  "command": { "kind": "clear_queue" }
}
```

Behaviour:

- If a `clear_queue` handler is installed, it is called with no arguments.

---

## Type: `"sequence"`

A sequence message wraps an ordered list of command objects, each with its own `kind` and associated fields.

```jsonc
{
  "type": "sequence",
  "steps": [
    { "kind": "drive", "direction": "forward", "speed": 100, "distance": 500 },
    { "kind": "wait", "duration": 1000 },
    { "kind": "turn", "radius": 200, "angle": 90, "speed": 100 },
    { "kind": "drive", "direction": "backward", "speed": 100, "distance": 200 }
  ]
}
```

Fields:

- **`steps`** (array, required)
  - Each element must be a JSON object that looks like the inner `command` object of a `type: "command"` message.
  - Each step must have a `kind` and fields appropriate to that kind (see above).

Behaviour:

- If `steps` is missing or not an array, the message is ignored with a warning.
- Every element of `steps` is processed in order by `handle_single_command_object()`.
  - Non‑object entries are skipped with a warning.
- This provides a simple **command queue in a single JSON document**.

---

## Type: `"config"`

A config message carries drive configuration data, typically sent once at startup or when tuning.

```jsonc
{
  "type": "config",
  "drive": {
    "wheel_track_mm": 120.0,
    "wheel_radius_mm": 30.0,
    "min_speed_mm_per_s": 20.0,
    "max_speed_mm_per_s": 200.0,
    "ticks_per_revolution": 360.0,
    "brake_on_stop": true,
    "enable_speed_control": true,
    "speed_kp": 0.5,
    "speed_ki": 0.1,
    "motor_gain_left": 1.0,
    "motor_gain_right": 1.0
  }
}
```

Fields:

- **`drive`** (object, required for drive config)
  - Parsed into `protocol_drive_config_t`.

Within `drive`:

- **`wheel_track_mm`** (number, optional)
  - Distance between wheels.
- **`wheel_radius_mm`** (number, optional)
  - Wheel radius.
- **`min_speed_mm_per_s`** (number, optional)
  - Minimum controllable speed.
- **`max_speed_mm_per_s`** (number, optional)
  - Maximum speed.
- **`ticks_per_revolution`** (number, optional)
  - Encoder ticks per wheel revolution.
- **`brake_on_stop`** (bool, optional)
  - If present and boolean, copied directly.
- **`enable_speed_control`** (bool, optional)
  - If present and boolean, copied directly.
- **`speed_kp`** (number, optional)
  - Proportional gain for speed control.
- **`speed_ki`** (number, optional)
  - Integral gain for speed control.
- **`motor_gain_left`** (number, optional)
  - Per‑motor gain calibration.
- **`motor_gain_right`** (number, optional)
  - Per‑motor gain calibration.

Behaviour:

- Missing fields are left as the default values in a zero‑initialised `protocol_drive_config_t`.
- If a `set_drive_config` handler is installed, it is called as:
  - `set_drive_config(&cfg)`.

---

## Error handling and logging

- Invalid or malformed JSON:
  - If parsing fails, an error is logged and the message is ignored.
- Missing or invalid `type`:
  - Logs a warning and discards the message.
- Unknown `type` values:
  - Logs a warning (`"Unknown message type"`).
- Missing `kind` or invalid fields:
  - Logs a warning and ignores that specific command or step.
- Unknown `kind` values:
  - Logs a warning (`"Unknown command kind"`).

All logging uses ESP‑IDF’s `ESP_LOG*` macros with tag `"protocol"`.

---

## Integrating the protocol module

### Registering handlers

To receive parsed commands, fill in a `protocol_handlers_t` instance and pass it to `protocol_set_handlers`:

```c
static void my_drive_handler(const char *direction,
                             int32_t speed_mm_per_s,
                             uint32_t duration_ms,
                             uint32_t distance_mm) {
  // Implement drive logic
}

static const protocol_handlers_t HANDLERS = {
  .drive = my_drive_handler,
  // .turn, .stop, .clear_queue, .set_led_hsv, .set_drive_config, .immediate ...
};

void app_init(void) {
  protocol_set_handlers(&HANDLERS);
}
```

### Feeding JSON into the parser

Whenever you receive a JSON message as a byte buffer:

```c
void on_message_received(const char *data, size_t len) {
  protocol_handle_command_json(data, len);
}
```

- `data` does not need to be null‑terminated; the function copies it into a temporary buffer and appends `\0`.

---

## Examples

### Simple drive forward 1 metre

```jsonc
{
  "type": "command",
  "command": {
    "kind": "drive",
    "direction": "forward",
    "speed": 150,
    "distance": 1000
  }
}
```

### Drive sequence with turn and wait

```jsonc
{
  "type": "sequence",
  "steps": [
    { "kind": "drive", "direction": "forward", "speed": 100, "distance": 500 },
    { "kind": "wait", "duration": 1000 },
    { "kind": "turn", "radius": 150, "angle": 90, "speed": 120 },
    { "kind": "drive", "direction": "forward", "speed": 100, "distance": 300 }
  ]
}
```

### Immediate joystick‑style command

```jsonc
{
  "type": "command",
  "command": {
    "kind": "immediate",
    "left": -0.2,
    "right": 0.8,
    "timeout_ms": 250
  }
}
```

This README fully specifies the JSON protocol implemented by `robot-protocol`. Keep it in sync with `protocol.c` and `protocol.h` when extending the protocol (e.g. adding new `kind` values or message types).
