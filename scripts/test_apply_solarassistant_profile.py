#!/usr/bin/env python3
"""
Test locali del configuratore MQTT, senza connessione al dispositivo.
Nessuna richiesta HTTP reale e nessuna conferma fisica simulata come vera.
"""

import contextlib
import copy
import io
import os
import unittest
from unittest.mock import patch, Mock

import apply_solarassistant_profile as target


def sample_config():
    return {
        "version": 2,
        "mqttHost": "192.168.1.240",
        "mqttPort": 1883,
        "mqttUser": "utente-esistente",
        "discovery": "solar_assistant/#",
        "topics": {
            "pv": "",
            "load": "",
            "battery": "",
            "voltage": "",
            "current": "",
            "soc": "",
            "inverter": "",
            "import": "",
            "export": "",
        },
        "automationEnabled": True,
        "ssid": "wifi-esistente",
        "adminUser": "admin",
        "staticIp": "192.168.1.242",
        "setpoint": 24,
        "irVerified": True,
        "energy": {
            "negativeCharge": True,
            "negativeCurrentCharge": True,
            "reserveW": 800,
            "minVoltage": 47,
        },
        "detector": {"deltaW": 200},
    }


class ProfileTests(unittest.TestCase):

    def test_only_expected_fields_change(self):
        existing = sample_config()
        before = copy.deepcopy(existing)
        new = target.create_target_config(existing)

        self.assertEqual(new["mqttHost"], target.BROKER_HOST)
        self.assertEqual(new["mqttPort"], target.BROKER_PORT)
        self.assertEqual(new["discovery"], "")
        self.assertEqual(new["topics"], target.TOPICS)
        self.assertIs(new["automationEnabled"], False)
        self.assertIs(new["energy"]["negativeCharge"], False)
        self.assertIs(new["energy"]["negativeCurrentCharge"], False)

        # La preparazione del payload non deve alterare l'oggetto originale.
        self.assertEqual(existing, before)

        for key in (
            "mqttUser", "ssid", "adminUser", "staticIp",
            "setpoint", "irVerified", "detector"
        ):
            self.assertEqual(new[key], existing[key])
        self.assertEqual(new["energy"]["reserveW"], 800)
        self.assertEqual(new["energy"]["minVoltage"], 47)

    def test_never_posts_secrets(self):
        existing = sample_config()
        existing["wifiPassword"] = "wifi-segreto"
        existing["mqttPassword"] = "mqtt-segreto"
        existing["adminPassword"] = "web-segreto"

        new = target.create_target_config(existing)

        for secret in ("wifiPassword", "mqttPassword", "adminPassword"):
            self.assertNotIn(secret, new)
            self.assertIn(secret, existing)

    def test_applied_profile_verification(self):
        expected = target.create_target_config(sample_config())
        self.assertTrue(target.verify_applied_profile(expected))
        expected["topics"]["pv"] = ""
        self.assertFalse(target.verify_applied_profile(expected))

    def test_popup_cancel_refuses_confirmation(self):
        with (
            patch.object(target.shutil, "which", return_value="/usr/bin/kdialog"),
            patch.dict(os.environ, {"DISPLAY": ":0"}),
            patch.object(target.subprocess, "run", return_value=Mock(returncode=1)),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertFalse(target.require_physical_off_popup("Salva"))

    def test_popup_missing_is_fail_closed(self):
        with patch.object(target.shutil, "which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "Finestra di conferma"):
                target.require_physical_off_popup("Salva")

    def test_cancel_never_sends_http_post(self):
        state = {
            "csrf": "token-di-test",
            "version": "0.3.6",
            "wifi": True,
            "mqtt": True,
            "uptime": 1800,
            "energy": {},
        }
        config = sample_config()
        calls = []

        def fake_request(base_url, path, authorization, csrf="", data=None,
                         timeout=15):
            calls.append((path, data))
            if path == "/api/state":
                return state
            if path == "/api/config":
                return config
            raise AssertionError("POST inatteso su " + path)

        with (
            patch.object(target, "request_json", side_effect=fake_request),
            patch.object(target, "require_physical_off_popup", return_value=False),
            patch.object(target.getpass, "getpass", return_value="web-segreto"),
            patch("builtins.input", return_value="admin"),
            patch.object(target.sys, "argv", ["script", "--host", "192.168.1.242"]),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            result = target.main()

        self.assertEqual(result, 1)
        self.assertTrue(calls)
        self.assertTrue(all(data is None for path, data in calls))

    def test_check_mode_never_sends_http_post(self):
        state = {
            "csrf": "token-di-test",
            "version": "0.3.6",
            "wifi": True,
            "mqtt": True,
            "uptime": 1800,
            "energy": {},
        }
        config = sample_config()
        calls = []

        def fake_request(base_url, path, authorization, csrf="", data=None,
                         timeout=15):
            calls.append((path, data))
            if path == "/api/state":
                return state
            if path == "/api/config":
                return config
            raise AssertionError("POST inatteso su " + path)

        with (
            patch.object(target, "request_json", side_effect=fake_request),
            patch.object(target.getpass, "getpass", return_value="web-segreto"),
            patch("builtins.input", return_value="admin"),
            patch.object(target.sys, "argv", ["script", "--check"]),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            result = target.main()

        self.assertEqual(result, 0)
        self.assertTrue(all(data is None for path, data in calls))


if __name__ == "__main__":
    unittest.main()
