"""Create legacy pubring.gpg (which pacman 6.1 stat-checks), rebuild trustdb,
and manually verify a repo DB signature with gpg.

MSYS2 root: set MSYS2_ROOT env var to override (default D:\\msys64)."""
import subprocess, os, urllib.request, shutil

MSYS2 = os.environ.get('MSYS2_ROOT', r'D:\msys64')
GPG = os.path.join(MSYS2, 'usr', 'bin', 'gpg.exe')

def posix(p):
    return '/' + p[0].lower() + p[2:].replace('\\', '/')

HW = os.path.join(MSYS2, 'etc', 'pacman.d', 'gnupg')
H = posix(HW)
env = dict(os.environ)
env['HOME'] = os.path.join(MSYS2, 'home', os.environ.get('USERNAME', 'user'))

def run(a, to=120):
    print('=' * 60)
    print('CMD:', ' '.join(a))
    p = subprocess.run(a, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=to)
    print('rc:', p.returncode)
    if p.stdout:
        print('--- stdout ---')
        print(p.stdout.decode('utf-8', 'replace')[:2500])
    if p.stderr:
        print('--- stderr ---')
        print(p.stderr.decode('utf-8', 'replace')[:2500])
    return p

# 1. export all keys into legacy pubring.gpg (gpg writes legacy format for .gpg)
run([GPG, '--homedir', H, '--batch', '--yes', '--export',
     '--output', H + '/pubring.gpg'])
print('pubring.gpg size:', os.path.getsize(os.path.join(HW, 'pubring.gpg')))

# 2. rebuild trust paths
run([GPG, '--homedir', H, '--batch', '--yes', '--check-trustdb'])

# 3. download mingw32.db + sig and verify with our keyring
tmp = os.path.join(MSYS2, 'tmp')
for f in ('mingw32.db', 'mingw32.db.sig'):
    u = 'https://mirrors.aliyun.com/msys2/mingw/mingw32/' + f
    dst = os.path.join(tmp, f)
    with urllib.request.urlopen(u, timeout=120) as r, open(dst, 'wb') as o:
        shutil.copyfileobj(r, o)
    print('downloaded', f, os.path.getsize(dst))

run([GPG, '--homedir', H, '--batch', '--verify',
     posix(tmp) + '/mingw32.db.sig', posix(tmp) + '/mingw32.db'])
