"""Equivalent of pacman-key --init + --populate msys2, done via direct gpg calls.
We use Python subprocess so argv is passed cleanly (no MSYS path conversion).

MSYS2 root: set MSYS2_ROOT env var to override (default D:\\msys64)."""
import subprocess, os, sys

MSYS2 = os.environ.get('MSYS2_ROOT', r'D:\msys64')
GPG = os.path.join(MSYS2, 'usr', 'bin', 'gpg.exe')

# msys2 binaries expect POSIX-style paths (/d/...), not Windows paths
def posix(p):
    return '/' + p[0].lower() + p[2:].replace('\\', '/')
HOMEDIR = posix(os.path.join(MSYS2, 'etc', 'pacman.d', 'gnupg'))
KEYRINGS = posix(os.path.join(MSYS2, 'usr', 'share', 'pacman', 'keyrings'))

env = dict(os.environ)
env['HOME'] = os.path.join(MSYS2, 'home', os.environ.get('USERNAME', 'user'))

def run(args, stdin_data=None):
    print('=' * 60)
    print('CMD:', ' '.join(args))
    try:
        p = subprocess.run(args, input=stdin_data,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           env=env, timeout=120)
    except Exception as e:
        print('EXC:', e)
        return None
    print('rc:', p.returncode)
    out = p.stdout.decode('utf-8', 'replace')
    err = p.stderr.decode('utf-8', 'replace')
    if out:
        print('--- stdout ---')
        print(out[:4000])
    if err:
        print('--- stderr ---')
        print(err[:4000])
    return p

os.makedirs(os.path.join(MSYS2, 'etc', 'pacman.d', 'gnupg'), exist_ok=True)

run([GPG, '--version'])
run([GPG, '--homedir', HOMEDIR, '--batch', '--import',
     KEYRINGS + '/msys2.gpg'])
run([GPG, '--homedir', HOMEDIR, '--batch', '--import-ownertrust',
     KEYRINGS + '/msys2-trusted'])
p = run([GPG, '--homedir', HOMEDIR, '--list-keys', '--with-colons'])
if p and p.stdout:
    n = p.stdout.decode('utf-8', 'replace').count('fpr:')
    print('imported key fprs:', n)
