# The full set of controls a device exposes (device -> operator).
#
# Republished on a periodic heartbeat with QoS RELIABLE + VOLATILE, never
# TRANSIENT_LOCAL (unh_marine_autonomy ADR-0003 D5): a late joiner gets fresh
# state from the next heartbeat, not a stale latched sample. header.stamp marks
# when the set was published, so a frozen panel is detectable (staleness).

std_msgs/Header header   # stamp = publish time (staleness); frame_id unused
string device_name       # Human name of the device/node, for the UI title
ControlItem[] items
