#!/usr/bin/env python3
"""Client QMP minimal pour piloter la VM Arfuso et capturer des écrans."""
import json, socket, sys, time

SOCK = sys.argv[1]
OPS = sys.argv[2:]          # ex: wait:3  key:ret  dump:build/shot.png

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(SOCK)
f = s.makefile('rwb', buffering=0)

def recv_json():
    line = f.readline()
    return json.loads(line.decode())

greeting = recv_json()
f.write(b'{"execute":"qmp_capabilities"}\n')
recv_json()

for op in OPS:
    cmd, _, arg = op.partition(':')
    if cmd == 'wait':
        time.sleep(float(arg))
    elif cmd == 'text':
        # AZERTY : pour que le noyau produise 'c', convertir la lettre en touche QWERTY a envoyer
        AZ = {'a':'q','q':'a','z':'w','w':'z','m':'semicolon',',':'m',';':'comma',':':'shift-semicolon'}
        for ch in arg:
            if ch == ' ':
                k = 'spc'
            elif ch == '.':
                k = 'dot'
            elif ch.isdigit():
                k = 'shift-' + ch          # en AZERTY les chiffres sont majuscules
            elif ch.isalpha():
                k = AZ.get(ch.lower(), ch.lower())
                if ch.isupper():
                    k = 'shift-' + k
            else:
                k = AZ.get(ch, None)
                if not k: continue
            f.write(json.dumps({"execute": "human-monitor-command",
                                "arguments": {"command-line": f"sendkey {k}"}}).encode() + b'\n')
            recv_json()
            time.sleep(0.12)
    elif cmd == 'key':
        f.write(json.dumps({"execute": "human-monitor-command",
                            "arguments": {"command-line": f"sendkey {arg}"}}).encode() + b'\n')
        recv_json()
        time.sleep(0.25)
    elif cmd == 'move':
        dx, _, dy = arg.partition(',')
        f.write(json.dumps({"execute": "human-monitor-command",
                            "arguments": {"command-line": f"mouse_move {dx} {dy}"}}).encode() + b'\n')
        recv_json()
        time.sleep(0.3)
    elif cmd == 'abs':        # position ABSOLUE en pixels ecran (usb-tablet)
        px, _, py = arg.partition(',')
        vx = max(0, min(0x7FFF, int(int(px) * 0x8000 / 1024)))
        vy = max(0, min(0x7FFF, int(int(py) * 0x8000 / 768)))
        ev = {"execute": "input-send-event", "arguments": {"events": [
            {"type": "abs", "data": {"axis": "x", "value": vx}},
            {"type": "abs", "data": {"axis": "y", "value": vy}}]}}
        f.write(json.dumps(ev).encode() + b'\n')
        recv_json()
        time.sleep(0.3)
    elif cmd == 'abtn':       # bouton gauche via evenement natif
        ev = {"execute": "input-send-event", "arguments": {"events": [
            {"type": "btn", "data": {"button": "left", "down": arg == "1"}}]}}
        f.write(json.dumps(ev).encode() + b'\n')
        recv_json()
        time.sleep(0.2)
    elif cmd == 'btn':
        f.write(json.dumps({"execute": "human-monitor-command",
                            "arguments": {"command-line": f"mouse_button {arg}"}}).encode() + b'\n')
        recv_json()
        time.sleep(0.15)
    elif cmd == 'dump':
        f.write(json.dumps({"execute": "screendump", "arguments": {"filename": arg, "format": "png"}}).encode() + b'\n')
        recv_json()
        time.sleep(0.3)
print("OK")
