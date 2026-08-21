import sys
import types
import unittest
from unittest.mock import patch


# The normalization tests do not open MQTT or serial transports. Stub the
# optional runtime dependencies so the bridge can be imported in a clean host
# test environment.
mqtt_client = types.ModuleType("paho.mqtt.client")
mqtt_client.MQTT_ERR_SUCCESS = 0
paho = types.ModuleType("paho")
paho_mqtt = types.ModuleType("paho.mqtt")
paho_mqtt.client = mqtt_client
paho.mqtt = paho_mqtt
serial = types.ModuleType("serial")
serial.SerialException = OSError

sys.modules.setdefault("paho", paho)
sys.modules.setdefault("paho.mqtt", paho_mqtt)
sys.modules.setdefault("paho.mqtt.client", mqtt_client)
sys.modules.setdefault("serial", serial)

import stm32_serial_mqtt_bridge as bridge


class NormalizeGatewayPayloadTests(unittest.TestCase):
    def test_schema_v2_preserves_node_and_snapshot_timestamps(self):
        source = {
            "schema_version": 2,
            "gateway_id": "gw-01",
            "timestamp": 1787232369207,
            "readings": [
                {
                    "node_id": "gw-01",
                    "timestamp": 1787232369207,
                    "rssi": -48,
                    "seq": 18,
                    "rssi_raw": -49,
                    "status": 0,
                }
            ],
        }

        normalized = bridge.normalize_gateway_payload(source, "gw-01")

        self.assertEqual(normalized["schema_version"], 2)
        self.assertEqual(normalized["timestamp"], 1787232369207)
        self.assertEqual(normalized["readings"][0]["timestamp"], 1787232369207)
        self.assertEqual(normalized["readings"][0]["rssi_raw"], -49)

    def test_schema_v2_preserves_invalid_time_status_and_zero_timestamp(self):
        source = {
            "schema_version": 2,
            "gateway_id": "gw-01",
            "timestamp": 0,
            "readings": [
                {
                    "node_id": 1,
                    "timestamp": 0,
                    "rssi": -53,
                    "seq": 7,
                    "status": 0x0010,
                }
            ],
        }

        normalized = bridge.normalize_gateway_payload(source, "gw-01")
        reading = normalized["readings"][0]

        self.assertEqual(reading["node_id"], "node-01")
        self.assertEqual(reading["timestamp"], 0)
        self.assertEqual(reading["status"], 0x0010)

    def test_schema_v2_does_not_replace_missing_node_timestamp(self):
        source = {
            "schema_version": 2,
            "gateway_id": "gw-01",
            "timestamp": 1787232369207,
            "readings": [{"node_id": 1, "rssi": -53, "seq": 7, "status": 0}],
        }

        with self.assertRaisesRegex(ValueError, "missing timestamp"):
            bridge.normalize_gateway_payload(source, "gw-01")

    def test_legacy_payload_still_uses_bridge_receipt_timestamp(self):
        source = {
            "device_id": "stm32-gw-01",
            "nodes": [
                {
                    "node_id": 5,
                    "rssi_filtered_x10": -598,
                    "rssi_raw_dbm": -62,
                    "seq": 2,
                    "error_flags": 3,
                }
            ],
        }

        with patch.object(bridge, "now_ms", return_value=1787232000000):
            normalized = bridge.normalize_gateway_payload(source, "gw-01")

        reading = normalized["readings"][0]
        self.assertEqual(normalized["schema_version"], 2)
        self.assertEqual(reading["node_id"], "gw-01")
        self.assertEqual(reading["timestamp"], 1787232000000)
        self.assertEqual(reading["rssi"], -60)
        self.assertEqual(reading["status"], 3)


class PositionTests(unittest.TestCase):
    def test_missing_position_does_not_publish_placeholder_coordinates(self):
        reading = {
            "node_id": "node-01",
            "timestamp": 1787232369207,
            "rssi": -48,
            "seq": 18,
            "status": 0,
        }

        positioned, found = bridge.attach_position(reading, {})

        self.assertFalse(found)
        self.assertNotIn("pos_x", positioned)
        self.assertNotIn("pos_y", positioned)
        self.assertNotIn("pos_z", positioned)


if __name__ == "__main__":
    unittest.main()
