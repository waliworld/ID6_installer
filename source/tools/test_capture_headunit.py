import argparse
import struct
import unittest
from unittest.mock import patch

from capture_headunit import FRAMEWORKS, JACINTO_FRAMEWORK_DIRS, capture, enet_ip, ssh_read

LIBRARY = b"\x7fELF\x01\x01" + bytes(10) + struct.pack("<HH", 3, 40) + b"example"


def mock_head(firmware=b"NBTevo_A20000I", valid_frameworks=True):
    def read(command, optional=False):
        if command == "cat /opt/sys/etc/nbt_version.txt":
            return firmware
        if command == "uname -a":
            return b"QNX private-hostname 6.5.0 build board armle"
        if command.endswith("get screen 0"):
            return b"- resolution: x=1280, y=480\n"
        if command in ("cat /opt/hmi/ID5/lib/" + name for name in FRAMEWORKS):
            return LIBRARY if valid_frameworks else b"invalid"
        raise AssertionError(command)
    return read


def mock_jacinto(framework_dir=JACINTO_FRAMEWORK_DIRS[0]):
    """Split-node unit: SSH on the Jacinto, HMI files only over Qnet."""
    def read(command, optional=False):
        if command == "cat /etc/nbt_version.txt":
            return b"NBTevo_P18362I"
        if command == "uname -a":
            return b"QNX hu-jacinto 6.5.0 2015/12/01 J5_Evo armle"
        if command.startswith("on -f hu-omap") and command.endswith('"'):
            return b"- resolution: x=1280, y=480\n"
        if command == "on -f hu-omap /sbin/ifconfig":
            return b"lo0: inet 127.0.0.1\ndwc0: inet 169.254.199.119 netmask 0xffff0000\n"
        if command in ("cat " + framework_dir + "/" + name for name in FRAMEWORKS):
            return LIBRARY
        if optional:  # a wrong Qnet mount point reads empty
            return b""
        raise AssertionError(command)
    return read


class CaptureTest(unittest.TestCase):
    def test_accepts_an_unlisted_firmware_and_omits_hostname(self):
        report, files = capture(mock_head(), None)
        self.assertEqual(report["firmware"], "NBTevo_A20000I")
        self.assertEqual(report["firmware_source"], "ssh")
        self.assertEqual(report["display"], {"width": 1280, "height": 480})
        self.assertNotIn("private-hostname", str(report))
        self.assertEqual(set(files), set(FRAMEWORKS))
        self.assertTrue(all(len(f["sha256"]) == 64 for f in report["frameworks"].values()))

    def test_reports_direct_node_without_leaking_hostname(self):
        report, _ = capture(mock_head(), None)
        self.assertEqual(report["ssh_node"], "unknown")
        self.assertNotIn("private-hostname", str(report))

    def test_split_node_reads_hmi_over_qnet(self):
        report, files = capture(mock_jacinto(), None)
        self.assertEqual(report["firmware"], "NBTevo_P18362I")
        self.assertEqual(report["ssh_node"], "hu-jacinto")
        self.assertEqual(report["framework_dir"], JACINTO_FRAMEWORK_DIRS[0])
        self.assertEqual(report["hmi_node_ip"], "169.254.199.119")
        self.assertEqual(report["display"], {"width": 1280, "height": 480})
        self.assertEqual(set(files), set(FRAMEWORKS))

    def test_split_node_falls_back_to_other_qnet_mount(self):
        report, _ = capture(mock_jacinto(framework_dir=JACINTO_FRAMEWORK_DIRS[1]), None)
        self.assertEqual(report["framework_dir"], JACINTO_FRAMEWORK_DIRS[1])

    def test_manual_version_and_invalid_framework(self):
        report, _ = capture(mock_head(b""), "NBTevo_A20000I")
        self.assertEqual(report["firmware_source"], "manual")
        with self.assertRaisesRegex(ValueError, "differs"):
            capture(mock_head(), "NBTevo_A20001I")
        with self.assertRaisesRegex(ValueError, "ELF32"):
            capture(mock_head(valid_frameworks=False), None)
        with self.assertRaises(argparse.ArgumentTypeError):
            enet_ip("192.168.1.2")

    @patch("capture_headunit.subprocess.run")
    def test_keeps_strict_host_key_checking_in_legacy_mode(self, run):
        run.return_value.returncode = 0
        run.return_value.stdout = b"example"
        self.assertEqual(ssh_read("169.254.10.3", True, "uname -a"), b"example")
        args = run.call_args.args[0]
        self.assertIn("StrictHostKeyChecking=yes", args)
        self.assertIn("HostKeyAlgorithms=+ssh-rsa", args)
        self.assertEqual(args[-2:], ["root@169.254.10.3", "uname -a"])


if __name__ == "__main__":
    unittest.main()
