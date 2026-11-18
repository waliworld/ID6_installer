"""Compare supplied BMW ELF layouts without executing them (pyelftools + capstone).

Instruction-prefix comparisons stop before the first in-function PC-relative
literal pool. They do not compare the pool's relocated addresses or its strings.
Reports contain hashes and symbol identities, never library payload bytes.

The reference is either a private framework pair (--reference-dir) or a
fingerprint of one (--fingerprint). A fingerprint holds symbol names, sizes,
relocation slots and SHA-256 digests, so it can be shared without the libraries.
Write one with --reference-dir and --emit-fingerprint.
"""
import argparse
import hashlib
import json
from pathlib import Path

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
from capstone.arm import ARM_OP_MEM, ARM_REG_PC
from elftools.elf.elffile import ELFFile


def inventory(path):
    with path.open('rb') as f:
        elf = ELFFile(f)
        assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_ARM'
        table = elf.get_section_by_name('.dynsym')
        symbols, relocations = {}, {}
        for s in table.iter_symbols():
            if not s['st_size'] or not isinstance(s['st_shndx'], int):
                continue
            sec = elf.get_section(s['st_shndx'])
            offset = s['st_value'] - sec['sh_addr']
            data = sec.data()[offset:offset + s['st_size']]
            symbols[s.name] = {'address': s['st_value'], 'data': data,
                               'type': s['st_info']['type']}
        for sec in elf.iter_sections():
            if sec['sh_type'] not in ('SHT_REL', 'SHT_RELA'):
                continue
            linked = elf.get_section(sec['sh_link'])
            for r in sec.iter_relocations():
                relocations[r['r_offset']] = [r['r_info_type'], linked.get_symbol(r['r_info_sym']).name]
        comment = elf.get_section_by_name('.comment')
        return symbols, relocations, comment.data().decode('ascii', 'replace').strip('\0') if comment else None


def prefix(symbol):
    data, address = symbol['data'], symbol['address']
    stop = len(data)
    decoder = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    decoder.detail = True
    for insn in decoder.disasm(data, address):
        if insn.address - address >= stop:
            break
        for operand in insn.operands:
            if operand.type == ARM_OP_MEM and operand.mem.base == ARM_REG_PC:
                offset = insn.address + 8 + operand.mem.disp - address
                if 0 <= offset < stop:
                    stop = offset
    return data[:stop]


LIBRARIES = ('libAppFramework.A.so', 'libCoreFramework.A.so')
VTABLES = ('_ZTVN3bmw9PluginAppE', '_ZTVN3bmw8RunnableE')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def constructor(name):
    return 'PluginAppC1E' in name or 'PluginAppC2E' in name


def fingerprint(directory, imports):
    frameworks = {}
    for name in LIBRARIES:
        path = directory / name
        symbols, relocations, comment = inventory(path)
        frameworks[name] = {
            'sha256': sha(path.read_bytes()), 'compiler_comment': comment,
            'functions': {n: {'bytes': len(s['data']), 'prefix_sha256': sha(prefix(s))}
                          for n, s in sorted(symbols.items())
                          if s['type'] == 'STT_FUNC' and (n in imports or constructor(n))},
            'objects': {n: {'bytes': len(s['data']), 'type': s['type']}
                        for n, s in sorted(symbols.items())
                        if n in imports and s['type'] != 'STT_FUNC'},
            'vtables': {n: {'bytes': len(symbols[n]['data']), 'sha256': sha(symbols[n]['data']),
                            'relocations': [relocations.get(symbols[n]['address'] + offset)
                                            for offset in range(0, len(symbols[n]['data']), 4)]}
                        for n in VTABLES if n in symbols}}
    return frameworks


def reference_gaps(reference, imports):
    covered = set().union(*(lib['functions'].keys() | lib['objects'].keys()
                            for lib in reference.values()))
    vtables = set().union(*(lib['vtables'].keys() for lib in reference.values()))
    return ([f'{name}: missing from reference' for name in sorted(imports - covered)]
            + [f'{name}: missing vtable in reference' for name in VTABLES if name not in vtables])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--reference-dir', type=Path)
    source.add_argument('--fingerprint', type=Path, help='fingerprint JSON to use as the reference')
    p.add_argument('--emit-fingerprint', type=Path, help='write the fingerprint of --reference-dir and exit')
    p.add_argument('--label', help='name of the reference firmware, stored in the fingerprint')
    p.add_argument('--installed-dir', type=Path)
    p.add_argument('--plugin', type=Path, required=True, nargs='+')
    p.add_argument('--report', type=Path)
    a = p.parse_args()
    imports = set()
    for plugin in a.plugin:
        with plugin.open('rb') as f:
            imports |= {s.name for s in ELFFile(f).get_section_by_name('.dynsym').iter_symbols()
                        if s['st_shndx'] == 'SHN_UNDEF' and 'bmw' in s.name}
    if a.emit_fingerprint:
        assert a.reference_dir, '--emit-fingerprint needs --reference-dir'
        reference = fingerprint(a.reference_dir, imports)
        gaps = reference_gaps(reference, imports)
        if gaps:
            p.error('; '.join(gaps))
        a.emit_fingerprint.write_text(json.dumps({
            'format': 2, 'reference': a.label, 'plugins': sorted(x.name for x in a.plugin),
            'frameworks': reference}, indent=1) + '\n')
        print('Fingerprint written:', a.emit_fingerprint)
        return
    assert a.installed_dir and a.report, '--installed-dir and --report are required'
    if a.fingerprint:
        stored = json.loads(a.fingerprint.read_text())
        if stored['format'] != 2:
            p.error('Expected fingerprint format 2. Regenerate the fingerprint.')
        reference = stored['frameworks']
    else:
        reference = fingerprint(a.reference_dir, imports)
    report = {'method': 'Offline ELF bytes, relocation tables and Capstone ARM instruction-prefix boundaries',
              'native_execution': False, 'frameworks': [], 'failures': reference_gaps(reference, imports),
              'limitations': ['Literal pools, runtime values and transitive callees are not semantically compared.',
                              'This comparison does not prove the reconstructed reference SDK correct.',
                              'Compiler behavior, HMI integration and lifecycle still require parked target tests.']}
    for name in LIBRARIES:
        ip, ref = a.installed_dir / name, reference[name]
        car, cr, cc = inventory(ip)
        entry = {'name': name, 'reference_sha256': ref['sha256'],
                 'installed_sha256': sha(ip.read_bytes()),
                 'installed_compiler_comment': cc, 'reference_compiler_comment': ref['compiler_comment'],
                 'function_comparisons': [], 'vtables': []}
        for n, expected in ref['objects'].items():
            if n not in imports:
                continue
            symbol = car.get(n)
            if symbol is None or symbol['type'] != expected['type'] or len(symbol['data']) != expected['bytes']:
                report['failures'].append(f'{name}: {n} missing or changed object type or size')
        for n, expected in ref['functions'].items():
            if n not in imports and not constructor(n):
                continue
            if n not in car or car[n]['type'] != 'STT_FUNC':
                report['failures'].append(f'{name}: missing function {n}')
                continue
            cb = prefix(car[n])
            same = sha(cb) == expected['prefix_sha256'] and len(car[n]['data']) == expected['bytes']
            entry['function_comparisons'].append({'symbol': n, 'installed_address': hex(car[n]['address']),
                'function_bytes': len(car[n]['data']), 'instruction_prefix_bytes': len(cb),
                'instruction_prefix_sha256': sha(cb), 'same_size_and_prefix': same})
            if not same:
                report['failures'].append(n + ': size/instruction prefix differs')
        for n, expected in ref['vtables'].items():
            if n not in car:
                report['failures'].append(f'{name}: missing vtable {n}')
                continue
            cs = car[n]
            slots = []
            for index, left in enumerate(expected['relocations']):
                right = cr.get(cs['address'] + index * 4)
                slots.append({'byte_offset': index * 4, 'relocation': right, 'equal': left == right})
            same = (len(cs['data']) == expected['bytes'] and sha(cs['data']) == expected['sha256']
                    and all(s['equal'] for s in slots))
            entry['vtables'].append({'symbol': n, 'same_bytes_and_relocation_slots': same, 'slots': slots})
            if not same:
                report['failures'].append(n + ': vtable differs')
        report['frameworks'].append(entry)
    comparisons = sum(len(x['function_comparisons']) for x in report['frameworks'])
    report['compared_functions'] = comparisons
    report['static_comparison_passed'] = not report['failures']
    a.report.write_text(json.dumps(report, indent=2) + '\n')
    if report['failures']:
        raise SystemExit('Framework comparison failed:\n' + '\n'.join(report['failures']))
    print('Framework comparison passed:', comparisons, 'function prefixes and PluginApp vtable; no target execution.')


if __name__ == '__main__':
    main()
