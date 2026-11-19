"""Exercise the installed ctypes binding using a synthetic Session."""

import os
from pathlib import Path
import struct
import tempfile
import unittest

from vaporview_session import Session, SessionError, Status

TIME_US = 1770000000000123
SEQUENCE = 9007199254741001


def waveform_payload(channel, samples, incomplete=False):
    points = len(samples)
    header = struct.pack("<4H4I2Q2I", 1, 48, channel, 1, 77, 100000, points,
                         0 if incomplete else points, 100200300400, 3, 4 if incomplete else 0, 0)
    return header + (b"" if incomplete else b"".join(struct.pack("<ii", *pair) for pair in samples))


def dat_record(payload, sequence):
    return struct.pack("<IIQIHHIQ", 0x44525756, 36, TIME_US, len(payload), 5, 2, 0, sequence) + payload


class SessionBindingTest(unittest.TestCase):
    def test_open_visit_waveform_export(self):
        library = os.environ.get("VAPORVIEW_TEST_SESSION_SDK_LIBRARY")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "session-会话"
            (source / "raw").mkdir(parents=True)
            (source / "sensors").mkdir()
            (source / "session.json").write_text(
                '{"session_format_version":1,"session_name":"fixture","state":"complete"}', encoding="utf-8")
            (source / "sensors" / "devices.csv").write_text(
                f"record_timestamp_us,nav_lat_deg\n{TIME_US},23.2\n{TIME_US},23.3\n{TIME_US + 1},23.4\n",
                encoding="utf-8")
            waves = struct.pack("<8sIIHH", b"VVRAWDAT", 2, 20, 5, 0)
            waves += dat_record(waveform_payload(48, [(16777217, -100), (2, -101)]), SEQUENCE)
            waves += dat_record(waveform_payload(49, [(0, 0)], incomplete=True), SEQUENCE + 1)
            (source / "raw" / "waveform.dat").write_bytes(waves + b"\x56\x57")
            output = root / "out-导出"
            output.mkdir()

            with Session(source, library=library) as session:
                self.assertTrue(session.version)
                self.assertEqual(session.metadata().value["session_name"], "fixture")
                self.assertEqual(session.categories().value, ["sensors", "waveforms"])
                seen = []
                result = session.visit("sensors", lambda record: seen.append(record) is None, TIME_US, TIME_US)
                self.assertEqual(result.records, 2)
                self.assertEqual([record.fields["nav_lat_deg"] for record in seen], ["23.2", "23.3"])
                cancelled = session.visit("sensors", lambda record: record.ordinal == 0)
                self.assertTrue(cancelled.cancelled)
                self.assertEqual(cancelled.records, 1)
                with self.assertRaisesRegex(ValueError, "callback failure"):
                    session.visit("sensors", self.failing_callback)
                with self.assertRaisesRegex(RuntimeError, "cannot reenter"):
                    session.visit("sensors", lambda record: session.metadata())
                with self.assertRaises(SessionError) as missing:
                    session.visit("pressure_raw", lambda record: True)
                self.assertEqual(missing.exception.status, Status.MISSING)

                first = session.read_waveform(0).value
                self.assertEqual(first.record.timestamp_us, TIME_US)
                self.assertEqual(first.record.sequence, SEQUENCE)
                self.assertEqual(first.h1, (16777217.0, 2.0))
                self.assertEqual(first.h2, (-100.0, -101.0))
                self.assertEqual(first.raw, ())
                self.assertEqual(first.harmonic, ())
                incomplete = session.read_waveform(1).value
                self.assertEqual(incomplete.h1, ())
                self.assertEqual(incomplete.h2, ())
                result = session.visit("waveforms", lambda record: True)
                self.assertEqual(result.records, 2)
                self.assertTrue(result.message)
                export = session.export("waveforms", output)
                self.assertEqual(export.value, 2)
                self.assertTrue(export.warning)
                self.assertTrue((output / "waveforms.jsonl").is_file())
            self.assertEqual(first.h1[0], 16777217.0)
            with self.assertRaisesRegex(RuntimeError, "closed"):
                session.metadata()
            session.close()

            with self.assertRaises(SessionError) as missing:
                Session(root / "absent", library=library)
            self.assertEqual(missing.exception.status, Status.INVALID_ARGUMENT)

    @staticmethod
    def failing_callback(record):
        raise ValueError("callback failure")


if __name__ == "__main__":
    unittest.main()
