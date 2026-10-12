"""The uniform blocks in a SPIR-V module, as the compiler laid them out: for each block, its
binding, its name, and every member's name, type and byte offset.

    import spirv
    blocks = spirv.uniform_blocks(Path('fs.spv').read_bytes())
    # {2: ('params', [('amplitude', 'float', 0), ...])}

SPIR-V is a documented binary format (Khronos, "SPIR-V Specification", sections 2.3 and
3): a five-word header, then instructions, each a word of (word count << 16 | opcode)
and its operands. This reads the handful of instructions that describe a uniform block
-- names, decorations, scalar, vector and struct types, pointers and variables -- and
nothing else. sokol-shdc writes the modules with --save-intermediate-spirv
(tools/pack_shader.py). libwgt's tools/spirv.py (wgrender's). Standard library only.
"""
import struct

MAGIC = 0x07230203

OP_NAME, OP_MEMBER_NAME = 5, 6
OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_VECTOR, OP_TYPE_STRUCT, OP_TYPE_POINTER = 21, 22, 23, 30, 32
OP_VARIABLE, OP_DECORATE, OP_MEMBER_DECORATE = 59, 71, 72
DECORATION_BLOCK, DECORATION_BINDING, DECORATION_OFFSET = 2, 33, 35
STORAGE_UNIFORM = 2


def _string(words):
    """A literal string operand: UTF-8, nul-terminated, padded to a whole word."""
    raw = struct.pack(f'<{len(words)}I', *words)
    return raw.split(b'\0', 1)[0].decode('utf-8')


def _instructions(data):
    """Each instruction past the header: (opcode, its operand words)."""
    if len(data) < 20 or len(data) % 4:
        raise ValueError('not a SPIR-V module: too short, or not whole words')
    order = '<' if struct.unpack_from('<I', data)[0] == MAGIC else '>'
    words = struct.unpack(f'{order}{len(data) // 4}I', data)
    if words[0] != MAGIC:
        raise ValueError('not a SPIR-V module: wrong magic number')
    at = 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 0xFFFF
        if count == 0 or at + count > len(words):
            raise ValueError(f'malformed instruction at word {at}')
        yield opcode, words[at + 1:at + count]
        at += count


def uniform_blocks(data):
    """{binding: (block name, [(member name, type, offset)])} for every uniform block. A
    type is 'float', 'int', 'uint', or 'vecN' / 'ivecN' / 'uvecN'; anything else (a
    matrix, an array, a nested struct) is 'type<id>', so a caller can refuse it by name."""
    names, member_names, scalars, vectors, structs = {}, {}, {}, {}, {}
    pointers, variables, bindings, blocks, offsets = {}, [], {}, set(), {}
    for op, args in _instructions(data):
        if op == OP_NAME:
            names[args[0]] = _string(args[1:])
        elif op == OP_MEMBER_NAME:
            member_names[(args[0], args[1])] = _string(args[2:])
        elif op == OP_TYPE_FLOAT:
            scalars[args[0]] = 'float'
        elif op == OP_TYPE_INT:
            scalars[args[0]] = 'int' if args[2] else 'uint'
        elif op == OP_TYPE_VECTOR:
            vectors[args[0]] = (args[1], args[2])
        elif op == OP_TYPE_STRUCT:
            structs[args[0]] = list(args[1:])
        elif op == OP_TYPE_POINTER:
            pointers[args[0]] = (args[1], args[2])
        elif op == OP_VARIABLE:
            variables.append((args[0], args[1], args[2]))
        elif op == OP_DECORATE:
            if args[1] == DECORATION_BLOCK:
                blocks.add(args[0])
            elif args[1] == DECORATION_BINDING:
                bindings[args[0]] = args[2]
        elif op == OP_MEMBER_DECORATE and args[2] == DECORATION_OFFSET:
            offsets[(args[0], args[1])] = args[3]

    def type_name(t):
        if t in scalars:
            return scalars[t]
        if t in vectors:
            component, count = vectors[t]
            prefix = {'float': '', 'int': 'i', 'uint': 'u'}.get(scalars.get(component), '?')
            return f'{prefix}vec{count}'
        return f'type{t}'

    out = {}
    for pointer_type, variable, storage in variables:
        if storage != STORAGE_UNIFORM or variable not in bindings:
            continue
        pointee = pointers.get(pointer_type, (None, None))[1]
        if pointee not in structs or pointee not in blocks:
            continue
        members = [(member_names.get((pointee, i), f'member{i}'), type_name(t), offsets.get((pointee, i), 0))
                   for i, t in enumerate(structs[pointee])]
        out[bindings[variable]] = (names.get(pointee, ''), members)
    return out


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('spirv.py: a module the other tools import: there is nothing to run')
