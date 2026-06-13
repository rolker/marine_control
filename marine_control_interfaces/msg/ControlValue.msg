# A request to change one control on a device (operator -> device).
#
# Fire-and-forget: there is no return code over a topic, so success is observed
# as the next ControlSet echo carrying the new value (unh_marine_autonomy
# ADR-0003 D1). header.stamp supports the change-audit required for safety-gated
# adopters (ADR-0003 D8.3).

std_msgs/Header header   # stamp = when the change was requested (audit)
string name              # Control to change; matches ControlItem.name
string value             # Requested value, stringified per the item's type
