# Download the MSVC-MingW (Windows) port of FreeRTOS-Kernel V10.4.3
import base64
import json
import os
import subprocess

CURL = 'curl'
API = ('https://api.github.com/repos/FreeRTOS/FreeRTOS-Kernel/contents/'
       'portable/MSVC-MingW/')
REF = '?ref=V10.4.3'
DEST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    'third_party', 'FreeRTOS-Kernel', 'portable', 'MSVC-MingW')

FILES = ['port.c', 'portmacro.h']


def fetch(rel):
    tmp = os.path.join(DEST, 'dl.tmp.json')
    url = API + rel + REF
    r = subprocess.run([CURL, '-sf', '--connect-timeout', '30', '-o', tmp, url],
                       timeout=120)
    if r.returncode != 0:
        raise RuntimeError('curl rc=%d for %s' % (r.returncode, url))
    with open(tmp, 'r', encoding='utf-8') as f:
        data = json.load(f)
    os.remove(tmp)
    if data.get('encoding') != 'base64':
        raise RuntimeError('unexpected encoding for %s' % rel)
    return base64.b64decode(data['content'])


def main():
    for rel in FILES:
        out = os.path.join(DEST, rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        content = fetch(rel)
        with open(out, 'wb') as f:
            f.write(content)
        print('OK  %-16s %d bytes' % (rel, len(content)))


if __name__ == '__main__':
    main()
