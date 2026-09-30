"""Install mingw-w64-i686-gcc via pacman (equivalent of `pacman -S`,
run from outside MSYS2 shell so argv passes cleanly).

MSYS2 root: set MSYS2_ROOT env var to override (default D:\\msys64)."""
import subprocess, os

MSYS2 = os.environ.get('MSYS2_ROOT', r'D:\msys64')
PAC = os.path.join(MSYS2, 'usr', 'bin', 'pacman.exe')
env = dict(os.environ)
env['HOME'] = os.path.join(MSYS2, 'home', os.environ.get('USERNAME', 'user'))

p = subprocess.run([PAC, '-S', '--noconfirm', '--needed', 'mingw-w64-i686-gcc'],
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
                   timeout=1800)
print('rc:', p.returncode)
print('OUT:', p.stdout.decode('utf-8', 'replace')[-4500:])
e = p.stderr.decode('utf-8', 'replace')
if e:
    print('ERR:', e[-2500:])
