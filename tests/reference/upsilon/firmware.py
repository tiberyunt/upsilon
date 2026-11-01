"""Reference-only ROM composition helper. Firmware assembly runs on the guest."""
import re

def compose_firmware(monitor, basic, native=""):
    """Link the existing BASIC source into the monitor with private symbols.

    BASIC BYE returns to the monitor; EOF shuts down. Standalone BASIC remains
    unchanged. Quoted strings/comments are not subject to symbol substitution.
    """
    basic = basic.replace('.entry start\n', '').replace('    .quad STACK, start\n', '')
    basic = basic.replace('do_bye:\n    JSR end_statement\n    HALT',
                          'do_bye:\n    JSR end_statement\n    JMP monitor_prompt')
    basic = basic.replace('    HALT\n', '    JMP monitor_shutdown\n')
    names = set(re.findall(r'^([A-Za-z_]\w*):', basic, re.M))
    names.update(re.findall(r'^\.equ\s+([A-Za-z_]\w*)', basic, re.M))
    pattern = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|;[^\n]*)|\b(' +
                         '|'.join(sorted(names, key=len, reverse=True)) + r')\b')
    basic = pattern.sub(lambda m: m[1] if m[1] is not None else 'basic_' + m[2], basic)
    return monitor + '\n; Linked BASIC interpreter\n' + basic + '\n' + native


def read_native_source(path):
    """Expand explicit source includes for reference tests, never guest input."""
    from pathlib import Path
    path = Path(path)
    return re.sub(r'^\.include "([^"\n]+)"\s*$',
                  lambda m: read_native_source(path.parent / m[1]),
                  path.read_text(), flags=re.M)
