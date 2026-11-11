# Writes dist\Lacrima.esp: an ESL-flagged plugin holding the two factions Lacrima puts
# actors into (src/Factions.cpp looks them up by these form IDs).
#
#   xx000800  LacrimaCryingFaction   while the actor cries
#   xx000801  LacrimaMascaraFaction  while run mascara shows on the face

import os
import struct

FORM_VERSION = 44
ESL = 0x200


def sub(tag, data):
    return tag + struct.pack('<H', len(data)) + data


def zstr(text):
    return text.encode('cp1252') + b'\x00'


def record(tag, flags, form_id, body):
    return tag + struct.pack('<IIIHHHH', len(body), flags, form_id, 0, 0, FORM_VERSION, 0) + body


def group(label, body):
    return b'GRUP' + struct.pack('<I', 24 + len(body)) + label + struct.pack('<IHHI', 0, 0, 0, 0) + body


def faction(form_id, editor_id, name):
    # DATA: flags (none: not hidden, not tracking crime)
    return record(b'FACT', 0, form_id, sub(b'EDID', zstr(editor_id)) + sub(b'FULL', zstr(name)) + sub(b'DATA', struct.pack('<I', 0)))


def main():
    # one master, so this plugin's own records sit at index 01
    factions = faction(0x01000800, 'LacrimaCryingFaction', 'Crying') + faction(0x01000801, 'LacrimaMascaraFaction', 'Mascara has run')
    body = group(b'FACT', factions)
    header = sub(b'HEDR', struct.pack('<fII', 1.7, 3, 0x802))  # 2 records + 1 group; next free object ID
    header += sub(b'CNAM', zstr('crajjjj'))
    header += sub(b'SNAM', zstr('Lacrima: the factions crying actors are put into.'))
    header += sub(b'MAST', zstr('Skyrim.esm')) + sub(b'DATA', struct.pack('<Q', 0))
    data = record(b'TES4', ESL, 0, header) + body
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dist', 'Lacrima.esp')
    open(path, 'wb').write(data)
    print('Lacrima.esp: %d bytes' % len(data))


if __name__ == '__main__':
    main()
