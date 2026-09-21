import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class SensorStatusChangeOnlyContractTest(unittest.TestCase):
    def test_status_notify_is_initial_and_change_only(self):
        advertising = (ROOT / "src/ble/ble_advertising.cpp").read_text()
        callbacks = (ROOT / "src/ble/ble_callbacks.cpp").read_text()

        self.assertIn("_sensorStatusNotifyPending ||", advertising)
        self.assertIn(
            "memcmp(_sensorStatusLastNotified, status, BLE_SENSOR_STATUS_SIZE) != 0",
            advertising,
        )
        self.assertIn("_pStatusChar->notify();", advertising)
        self.assertIn("_sensorStatusNotifyPending = false;", advertising)
        self.assertIn("_sensorStatusNotifyPending = true;", callbacks)


if __name__ == "__main__":
    unittest.main()
