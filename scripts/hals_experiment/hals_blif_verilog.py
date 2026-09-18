"""Emit scalar Boolean equations without Yosys's LUT-as-variable-shift syntax."""
from __future__ import annotations
from pathlib import Path


def boolean_verilog(blif, name, io, *, allow_gates=False):
    lines = []
    buffer = ''
    for line in Path(blif).read_text().splitlines():
        if line.endswith('\\'):
            buffer += line[:-1] + ' '
            continue
        lines.append(buffer + line)
        buffer = ''
    ids = {}
    def signal(raw):
        if raw not in ids:
            ids[raw] = 'v' + str(len(ids))
        return ids[raw]
    inputs, outputs, gates = [], [], []
    index = 0
    while index < len(lines):
        words = lines[index].split()
        index += 1
        if not words:
            continue
        op = words[0]
        if op == '.inputs':
            inputs = [signal(n) for n in words[1:]]
        elif op == '.outputs':
            outputs = [signal(n) for n in words[1:]]
        elif op == '.names':
            args = [signal(n) for n in words[1:-1]]
            output = signal(words[-1])
            cubes = []
            polarity = True
            while index < len(lines) and not lines[index].startswith('.'):
                truth = lines[index].split()
                index += 1
                if not truth or truth[0].startswith('#'):
                    continue
                cube, value = (truth[0], truth[1]) if args else ('', truth[0])
                if len(cube) != len(args) or any(c not in '01-' for c in cube):
                    raise ValueError('Invalid BLIF cube')
                if cubes and polarity != (value == '1'):
                    raise ValueError('Mixed cover polarity')
                polarity = value == '1'
                terms = [n if c == '1' else '~' + n for n, c in zip(args, cube) if c != '-']
                cubes.append('(' + ' & '.join(terms) + ')' if terms else "1'b1")
            expr = ' | '.join(cubes) if cubes else "1'b0"
            if cubes and not polarity:
                expr = '~(' + expr + ')'
            gates.append(f'  assign {output} = {expr};')
        elif op in ('.gate', '.subckt') and allow_gates:
            if words[1] in ('_const0_', '_const1_'):
                if len(words) != 3:
                    raise ValueError('Unexpected ABC constant gate pins')
                raw = words[2].split('=', 1)[1]
                value = '1' if words[1] == '_const1_' else '0'
                gates.append(f"  assign {signal(raw)} = 1'b{value};")
                continue
            connections = []
            for binding in words[2:]:
                pin, raw = binding.split('=', 1)
                connections.append(f'.{pin}({signal(raw)})')
            gates.append(f'  {words[1]} cell_{index} (' + ', '.join(connections) + ');')
        elif op in ('.latch', '.gate', '.subckt'):
            raise ValueError('Expected a combinational .names BLIF')
    ports = []
    bits = {'input': [], 'output': []}
    for port, spec in io.items():
        width = spec['width']
        direction = spec['direction']
        sign = ' signed' if spec.get('signed') else ''
        ports.append(f'  {direction}{sign} [{width-1}:0] {port}')
        bits[direction].extend(f'{port}[{i}]' for i in range(width))
    if len(inputs) != len(bits['input']) or len(outputs) != len(bits['output']):
        raise ValueError('BLIF / partition bit width mismatch')
    result = [f'module {name}(\n' + ',\n'.join(ports) + '\n);']
    result += [f'  wire {n};' for n in ids.values()]
    result += [f'  assign {n} = {p};' for n, p in zip(inputs, bits['input'])]
    result += gates
    result += [f'  assign {p} = {n};' for n, p in zip(outputs, bits['output'])]
    result.append('endmodule\n')
    return '\n'.join(result)
