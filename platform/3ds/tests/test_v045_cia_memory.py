import importlib.util
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('cia_memory', ROOT/'platform/3ds/tools/cia-memory.py')
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)


class CiaMemoryTests(unittest.TestCase):
    def test_new3ds_only_flag_cleared_keeping_icons_titles_and_other_flags(self):
        source = bytearray((i % 251 for i in range(0x36C0)))
        source[:4] = b'SMDH'
        struct.pack_into('<I', source, 0x2028, 0x1141)
        cleared = memory.clear_new3ds_only(source)
        self.assertEqual(cleared[:0x2028], source[:0x2028])
        self.assertEqual(cleared[0x202C:], source[0x202C:])
        self.assertEqual(struct.unpack_from('<I', cleared, 0x2028)[0], 0x141)
        self.assertEqual(memory.clear_new3ds_only(cleared), cleared)
        with self.assertRaises(ValueError):
            memory.clear_new3ds_only(source[:-1])

    def test_packaged_modes_required_and_new3ds_only_flag_refused(self):
        # Minimal unencrypted CIA containing the actual NCCH/ExeFS field layout.
        cia = bytearray(0x5000)
        struct.pack_into('<IHHIIIIQ', cia, 0, 0x40, 0, 0, 0, 0, 0, 0, 0)
        ncch = 0x40
        cia[ncch+0x100:ncch+0x104] = b'NCCH'
        cia[ncch+0x400+13] = 1
        cia[ncch+0x400+14] = 0x34
        struct.pack_into('<I', cia, ncch+0x1A0, 5)
        exefs = ncch + 5*512
        struct.pack_into('<8sII', cia, exefs, b'icon', 0, 0x36C0)
        icon = exefs + 512
        cia[icon:icon+4] = b'SMDH'
        struct.pack_into('<I', cia, icon+0x2028, 0x141)
        memory.verify(cia)
        for offset, value in ((ncch+0x400+13, 0), (ncch+0x400+14, 0x24), (ncch+0x400+12, 3),
                              (icon+0x2029, 0x11)):
            broken = bytearray(cia)
            broken[offset] = value
            with self.assertRaises(ValueError):
                memory.verify(broken)
        with self.assertRaises(ValueError):
            memory.verify(cia[:icon+20])
