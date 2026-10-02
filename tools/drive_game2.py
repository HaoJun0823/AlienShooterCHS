# -*- coding: utf-8 -*-
"""进关卡 / 难度界面的探索脚本：python drive_game2.py <tag> --logall"""
import subprocess, time, os, sys, ctypes
from ctypes import wintypes
from PIL import ImageGrab
GAME = r'I:/LocalGames/AlienShooter v1.22DIC'
OUT  = r'G:/Projects/AlienShooterCHS/tools_out'
UPD  = os.path.join(GAME, 'update')
u32 = ctypes.windll.user32
tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
logall = '--logall' in sys.argv
def wpath(n): return os.path.join(UPD, n)
try: os.remove(wpath('chs_probe.log'))
except OSError: pass
for f in os.listdir(UPD):
    if f.startswith('chs_off') or f.startswith('chs_logall'):
        try: os.remove(wpath(f))
        except OSError: pass
if logall: open(wpath('chs_logall.txt'), 'w').close()
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
def click(h, l, tp, r, b, fx, fy):
    u32.SetForegroundWindow(h); u32.BringWindowToTop(h); time.sleep(0.35)
    x, y = int(l + (r - l) * fx), int(tp + (b - tp) * fy)
    u32.SetCursorPos(x, y); time.sleep(0.25)
    u32.mouse_event(0x0002, 0, 0, 0, 0); u32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.3); print('  click (%.3f,%.3f)' % (fx, fy))
def key(vk, n=1):
    u32.SetForegroundWindow(h_g); time.sleep(0.15)
    for _ in range(n):
        u32.keybd_event(vk, 0, 0, 0); time.sleep(0.08)
        u32.keybd_event(vk, 0, 2, 0); time.sleep(0.25)
def shot(l, tp, r, b, name, tries=4):
    best = None
    for _ in range(tries):
        u32.SetForegroundWindow(h_g); u32.BringWindowToTop(h_g); time.sleep(0.4)
        im = ImageGrab.grab().crop((l, tp, r, b))
        nz = sum(1 for v in im.convert('L').getdata() if v > 12)
        if best is None or nz > best[0]: best = (nz, im)
        if nz > 20000: break
        time.sleep(0.4)
    best[1].save(os.path.join(OUT, 's_%s_%s.png' % (tag, name)))
    print('shot %s nonblack=%d' % (name, best[0]))
proc = subprocess.Popen([os.path.join(GAME, 'AlienShooter.exe')], cwd=GAME)
time.sleep(6)
w = find_game()
h_g, l, tp, r, b = w[0]
u32.ShowWindow(h_g, 9); u32.SetWindowPos(h_g, -1, 0, 0, 0, 0, 0x0001 | 0x0002); time.sleep(0.5)
print('window', (l, tp, r, b))
click(h_g, l, tp, r, b, 0.50, 0.90)
time.sleep(2.5)
shot(l, tp, r, b, 'menu')
click(h_g, l, tp, r, b, 0.27, 0.567)      # 新游戏
time.sleep(3.0)
shot(l, tp, r, b, 'profile')              # 角色/战役 界面
click(h_g, l, tp, r, b, 0.513, 0.575)     # 第一个按钮（战役）
time.sleep(3.0)
shot(l, tp, r, b, 'after_campaign')
key(0x27); time.sleep(1.2); shot(l, tp, r, b, 'right1')
key(0x27); time.sleep(1.2); shot(l, tp, r, b, 'right2')
key(0x25); time.sleep(1.2); shot(l, tp, r, b, 'left1')
key(0x0D); time.sleep(4.0); shot(l, tp, r, b, 'enter')
time.sleep(4.0); shot(l, tp, r, b, 'enter_b')
time.sleep(4.0); shot(l, tp, r, b, 'enter_c')
subprocess.run(['taskkill', '/F', '/IM', 'AlienShooter.exe'], capture_output=True)
if logall:
    try: os.remove(wpath('chs_logall.txt'))
    except OSError: pass
print('done')
