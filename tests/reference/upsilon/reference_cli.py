"""Command-line assembler, byte decoder, and bounded simulator."""
import argparse
from contextlib import ExitStack
import json
import sys
from pathlib import Path
from .assembler import assemble, Program
from .cpu import CPU, Memory
from .decoder import decode, format_instruction
from .isa import AssemblyError, Fault
from .devices import SerialConsole
from .keyboard import Keyboard

def number(text):
    return int(text, 0)

def state(cpu):
    system = cpu.sys.copy()
    system['PSR'] = cpu.psr
    system[('SPK','SPS','SPU')[cpu.mode]] = cpu.r[31]
    return {'pc': cpu.pc, 'psr': cpu.psr, 'fpsr': cpu.fpsr, 'steps': cpu.steps,
            'halted': cpu.halted, 'waiting': cpu.waiting,
            'registers': {f'R{i}': value for i, value in enumerate(cpu.r)},
            'float_bits': {f'F{i}': value for i, value in enumerate(cpu.f)},
            'system_registers': system}

def main(argv=None):
    parser = argparse.ArgumentParser(prog='upsilon', description='Kotl Upsilon v0.2 reference toolchain')
    commands = parser.add_subparsers(dest='command', required=True)
    assembler = commands.add_parser('asm', help='assemble source to a flat little-endian binary')
    assembler.add_argument('source', type=Path)
    assembler.add_argument('-o', '--output', type=Path, required=True)
    assembler.add_argument('--origin', type=number, default=0x1000)
    assembler.add_argument('--map', type=Path, help='write origin, entry, and symbol addresses as JSON')
    disassembler = commands.add_parser('disasm', help='decode a flat binary')
    disassembler.add_argument('binary', type=Path)
    disassembler.add_argument('--origin', type=number, default=0x1000)
    runner = commands.add_parser('run', help='assemble and run source, or run an existing binary')
    runner.add_argument('program', type=Path)
    runner.add_argument('--binary', action='store_true')
    runner.add_argument('--boot', action='store_true', help='protect image as ROM and reset from its SP/PC header')
    runner.add_argument('--serial', action='store_true', help='connect the MMIO serial console to stdout (stderr with --json)')
    runner.add_argument('--keyboard', action='store_true', help='read live keyboard or piped stdin through the serial port; implies --serial')
    runner.add_argument('--serial-input', default='', help='preload UTF-8 receive data; requires --serial')
    runner.add_argument('--origin', type=number, default=0x1000)
    runner.add_argument('--entry', type=number)
    runner.add_argument('--memory', type=number, default=1 << 20, help='physical RAM in bytes')
    runner.add_argument('--max-steps', type=number, default=1000000)
    runner.add_argument('--traps', action='store_true', help='deliver faults through VBR instead of stopping')
    runner.add_argument('--trace', action='store_true')
    runner.add_argument('--json', action='store_true', help='print machine-readable final state')
    runner.add_argument('--state', type=Path, help='also save final state as JSON, including on failure')
    args = parser.parse_args(argv)
    cpu = None
    try:
        if args.command == 'asm':
            program = assemble(args.source.read_text(), args.origin)
            args.output.write_bytes(program.code)
            if args.map:
                args.map.write_text(json.dumps({'origin': program.origin, 'entry': program.entry, 'symbols': program.symbols}, indent=2) + '\n')
            print(f'{len(program.code)} bytes at 0x{program.origin:x}; entry 0x{program.entry:x}')
        elif args.command == 'disasm':
            raw, offset = args.binary.read_bytes(), 0
            def fetch(address):
                index = address - args.origin
                if not 0 <= index < len(raw):
                    raise Fault(5, 'truncated instruction', address)
                return raw[index]
            while offset < len(raw):
                pc = args.origin + offset
                try:
                    ins = decode(fetch, pc)
                    length, text = ins.end - pc, format_instruction(ins)
                except Fault as exc:
                    length, text = 1, f'.byte 0x{raw[offset]:02x} ; {exc}'
                print(f'{pc:016x}: {raw[offset:offset + length].hex(" "):<40} {text}')
                offset += length
        else:
            args.serial = args.serial or args.keyboard
            if args.max_steps <= 0:
                raise ValueError('--max-steps must be positive')
            if args.boot and args.entry is not None:
                raise ValueError('--boot uses the ROM entry header and cannot be combined with --entry')
            if args.serial_input and not args.serial:
                raise ValueError('--serial-input requires --serial')
            program = (Program(args.program.read_bytes(), args.origin, args.origin, {}) if args.binary
                       else assemble(args.program.read_text(), args.origin))
            def serial_output(data):
                stream = sys.stderr.buffer if args.json else sys.stdout.buffer
                stream.write(data)
                stream.flush()
            serial = SerialConsole(args.serial_input.encode('utf-8'), serial_output) if args.serial else None
            cpu = CPU(Memory(args.memory, serial=serial), handle_traps=args.traps)
            if args.boot:
                cpu.memory.load_rom(program.origin, program.code)
                cpu.reset(program.origin)
            else:
                cpu.load(program)
            if args.entry is not None:
                cpu.pc = args.entry
            def trace(ins, machine):
                print(f'{ins.start:016x}: {format_instruction(ins):<48} R0={machine.r[0]:016x} PSR={machine.psr:04x}', file=sys.stderr)
            with ExitStack() as context:
                if args.keyboard:
                    serial.keyboard = context.enter_context(Keyboard())
                elif args.serial_input:
                    serial.eof = True
                cpu.run(args.max_steps, trace if args.trace else None)
            if args.json:
                print(json.dumps(state(cpu), indent=2))
            else:
                report = sys.stderr if args.serial else sys.stdout
                print(f'{"halted" if cpu.halted else "waiting"} after {cpu.steps} steps; PC=0x{cpu.pc:x}; PSR=0x{cpu.psr:x}', file=report)
                for start in range(0, 32, 4):
                    print('  '.join(f'R{i:02}={cpu.r[i]:016x}' for i in range(start, start + 4)), file=report)
        return 0
    except KeyboardInterrupt:
        print('\nupsilon: interrupted', file=sys.stderr)
        return 130
    except (AssemblyError, Fault, OSError, RuntimeError, ValueError) as exc:
        detail = f'vector {exc.vector}, address 0x{exc.address:x}: ' if isinstance(exc, Fault) else ''
        pc = f' at PC 0x{cpu.pc:x}' if cpu is not None else ''
        print(f'upsilon{pc}: {detail}{exc}', file=sys.stderr)
        return 1
    finally:
        if cpu is not None and args.state:
            args.state.write_text(json.dumps(state(cpu), indent=2) + '\n')

if __name__ == "__main__":
    raise SystemExit(main())
