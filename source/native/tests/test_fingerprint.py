"""The committed framework fingerprint must describe the packaged plugins."""
from copy import deepcopy
from io import BytesIO
import json
import re
import sys
import unittest
import zipfile
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'source/native/tools'))
from audit_framework_abi import VTABLES, reference_gaps

FINGERPRINT = json.loads((ROOT / 'source/native/framework-fingerprint.json').read_text())
DIGEST = re.compile(r'[0-9a-f]{64}')


class FingerprintTest(unittest.TestCase):
    def test_covers_every_packaged_plugin_import(self):
        packaged = []
        imports = set()
        for package in (ROOT / 'packages').glob('*.zip'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not re.fullmatch(r'[^/]+/lib[^/]+\.so', name):
                        continue
                    packaged.append(Path(name).name)
                    symbols = ELFFile(BytesIO(archive.read(name))).get_section_by_name('.dynsym')
                    imports.update(symbol.name for symbol in symbols.iter_symbols()
                                   if symbol['st_shndx'] == 'SHN_UNDEF' and 'bmw' in symbol.name)
        self.assertEqual(FINGERPRINT['plugins'], sorted(packaged))
        self.assertEqual(reference_gaps(FINGERPRINT['frameworks'], imports), [])

    def test_reports_missing_imports_and_vtables(self):
        frameworks = deepcopy(FINGERPRINT['frameworks'])
        symbol = '_ZN3bmw11BasicStringIcEC1EPKc'
        for library in frameworks.values():
            library['functions'].pop(symbol, None)
            library['vtables'].pop(VTABLES[1], None)
        self.assertEqual(reference_gaps(frameworks, {symbol}), [
            f'{symbol}: missing from reference',
            f'{VTABLES[1]}: missing vtable in reference',
        ])

    def test_holds_digests_and_a_plugin_app_vtable(self):
        self.assertEqual(FINGERPRINT['format'], 2)
        self.assertEqual(sorted(FINGERPRINT['frameworks']), ['libAppFramework.A.so', 'libCoreFramework.A.so'])
        functions = [f for lib in FINGERPRINT['frameworks'].values() for f in lib['functions'].values()]
        self.assertGreaterEqual(len(functions), 35)
        for function in functions:
            self.assertEqual(sorted(function), ['bytes', 'prefix_sha256'])
            self.assertTrue(DIGEST.fullmatch(function['prefix_sha256']))
        self.assertTrue(any('_ZTVN3bmw9PluginAppE' in lib['vtables'] for lib in FINGERPRINT['frameworks'].values()))


if __name__ == '__main__':
    unittest.main()
