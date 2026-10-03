# -*- coding: utf-8 -*-
"""启动游戏 → 点击进入菜单 → 截游戏窗口 → 关掉。用法：
   python run_game_shot.py <tag> [switch_file ...]
"""
import subprocess, time, os, sys, ctypes
from ctypes import wintypes
from PIL import ImageGrab

GAME = r'I:/LocalGames/AlienShooter v1.22DIC'
OUT  = r'G:/Projects/AlienShooterCHS/tools_out'
UPD  = os.path.join(GAME, 'update')
u32 = ctypes.windll.user32

tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
switches = sys.argv[2:]

# 清日志 + 布置开关
try: os.remove(os.path.join(UPD, 'chs_probe.log'))
except OSError: pass
for f in os.listdir(UPD):
    if f.startswith('chs_exp') or f == 'chs_off.txt':
        try: os.remove(os.path.join(UPD, f))
        except OSError: pass
for sw in switches:
    open(os.path.join(UPD, sw), 'w').close()

def find_game():
    res = []
    def cb(h, l):
        if not u32.IsWindowVisible(h): return True
        n = u32.GetWindowTextLengthW(h)
        if n:
            b = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(h, b, n + 1)
            if b.value == 'AlienShooter':
                r = wintypes.RECT(); u32.GetWindowRect(h, ctypes.byref(r))
                res.append((h, r.left, r.top, r.right, r.bottom))
        return True
    u32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)(cb), 0)
    return res

p = subprocess.Popen([os.path.join(GAME, 'AlienShooter.exe')], cwd=GAME)
time.sleep(6)
w = find_game()
if not w:
    subprocess.run(['taskkill', '/F', '/IM', 'AlienShooter.exe'], capture_output=True)
    print('NO WINDOW'); sys.exit(1)
h, l, tp, r, b = w[0]
u32.ShowWindow(h, 9)
u32.SetWindowPos(h, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
time.sleep(0.5)
u32.SetForegroundWindow(h); time.sleep(0.8)
u32.SetCursorPos((l + r) // 2, (tp + b) // 2); time.sleep(0.3)
u32.mouse_event(0x0002, 0, 0, 0, 0); u32.mouse_event(0x0004, 0, 0, 0, 0)
time.sleep(7)
best = None
for attempt in range(8):
    u32.SetForegroundWindow(h)
    u32.BringWindowToTop(h)
    time.sleep(0.6)
    im = ImageGrab.grab().crop((l, tp, r, b))
    px = im.convert('L').getdata()
    nz = sum(1 for v in px if v > 12)
    print('  attempt %d: non-black px = %d' % (attempt, nz))
    if best is None or nz > best[0]:
        best = (nz, im)
    if nz > 20000:
        break
    time.sleep(1.0)
if best and best[0] > 500:
    best[1].save(os.path.join(OUT, 'shot_%s.png' % tag))
    print('saved shot_%s.png (nonblack=%d)' % (tag, best[0]))
else:
    print('WARN: capture mostly black, saved anyway')
    best[1].save(os.path.join(OUT, 'shot_%s.png' % tag))
subprocess.run(['taskkill', '/F', '/IM', 'AlienShooter.exe'], capture_output=True)
for sw in switches:
    try: os.remove(os.path.join(UPD, sw))
    except OSError: pass
