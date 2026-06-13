# One controllable (or read-only) setting on a device, with its current value
# and everything a generic UI needs to render and edit it.
#
# Modeled on marine_radar_control_msgs/RadarControlItem, generalized per
# unh_marine_autonomy ADR-0003 (D3): adds INT/BOOL/STRING types, units, step,
# group, description, and read_only.

# Value type: how `value` (and the bounds) are interpreted/rendered.
uint8 TYPE_FLOAT  = 0
uint8 TYPE_INT    = 1
uint8 TYPE_BOOL   = 2
uint8 TYPE_STRING = 3
uint8 TYPE_ENUM   = 4

string   name          # Stable identifier; matches ControlValue.name on a change
string   label         # Human label for the UI (falls back to name if empty)
string   value         # Current value, stringified per `type`
uint8    type          # One of TYPE_*

# Numeric bounds (FLOAT/INT). float64 holds both; the UI casts per `type`.
float64  min_value     # Lower bound (ignored for BOOL/STRING/ENUM)
float64  max_value     # Upper bound (ignored for BOOL/STRING/ENUM)
float64  step          # Increment for sliders/spinners (0 = unspecified)

string[] enums         # Allowed choices when type == TYPE_ENUM

string   units         # Unit label, e.g. "m", "dB", "Hz" ("" if none)
string   group         # UI grouping for related controls ("" = ungrouped)
string   description    # Help text (mirrors a ParameterDescriptor description)
bool     read_only     # True = display-only telemetry, not settable
