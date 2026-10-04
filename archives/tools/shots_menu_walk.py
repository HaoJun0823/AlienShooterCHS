# -*- coding: utf-8 -*-
"""走一遍菜单流程，逐屏截图，用来定位「结算/成绩」这类界面到底长什么样。
用法: python shots_menu_walk.py <tag>
"""
import subprocess, time, os, sys, ctypes
from ctypes import wintypes
from PIL import ImageGrab

GAME = r'I:/LocalGames/AlienShooter v1.22DIC'
OUT  = r'G:/Projects/AlienShooterCHS/archives/screenshots_tmp'
UPD  = os.path.join(GAME, 'update')
u32  = ctypes.windll.user32

tag = sys.argv[1] if len(sys.argv) > 1 else 'w'
os.makedirs(OUT, exist_ok=True)

# 清开关
for f in os.listdir(UPD):
    if f.startswith('chs_off') or f.startswith('chs_logall') or f.startswith('chs_mapdiag'):
        try: os.remove(os.path.join(UPD, f))
        except OSError: pass
try: os.remove(os.path.join(UPD, 'chs_probe.log'))
except OSError: pass
open(os.path.join(UPD, 'chs_logall.txt'), 'w').close()


def find_game():
    res = []

    def cb(h, l):
        if not u32.IsWindowVisible(h):
            return True
        n = u32.GetWindowTextLengthW(h)
        if n:
            b = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(h, b, n + 1)
            if b.value == 'AlienShooter':
                r = wintypes.RECT()
                u32.GetWindowRect(h, ctypes.byref(r))
                res.append((h, r.left, r.top, r.right, r.bottom))
        return True

    u32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)(cb), 0)
    return res


proc = subprocess.Popen([os.path.join(GAME, 'AlienShooter.exe')], cwd=GAME)
time.sleep(7)
w = find_game()
if not w:
    print('NO WINDOW')
    sys.exit(1)
h, l, tp, r, b = w[0]
u32.ShowWindow(h, 9)
u32.SetWindowPos(h, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
time.sleep(0.6)


def shot(name):
    u32.SetForegroundWindow(h)
    u32.BringWindowToTop(h)
    time.sleep(0.8)
    im = ImageGrab.grab().crop((l, tp, r, b))
    p = os.path.join(OUT, 'w_%s_%s.png' % (tag, name))
    im.save(p)
    nz = sum(1 for v in im.convert('L').getdata() if v > 12)
    print('shot', name, 'nonblack=', nz, '->', p)


def click(fx, fy):
    u32.SetForegroundWindow(h)
    u32.BringWindowToTop(h)
    time.sleep(0.4)
    x, y = int(l + (r - l) * fx), int(tp + (b - tp) * fy)
    u32.SetCursorPos(x, y)
    time.sleep(0.3)
    u32.mouse_event(0x0002, 0, 0, 0, 0)
    u32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.6)


def key(vk):
    u32.SetForegroundWindow(h)
    time.sleep(0.3)
    u32.keybd_event(vk, 0, 0, 0)
    u32.keybd_event(vk, 0, 2, 0)
    time.sleep(0.6)


# ① 启动画面（点击继续）
shot('00_boot')
click(0.50, 0.90)
time.sleep(2.5)
shot('01_click')

# ② 主菜单 → 新游戏
click(0.27, 0.567)
time.sleep(3.0)
shot('02_saveslot')

# ③ 选存档（第一个人物卡）
click(0.40, 0.45)
time.sleep(2.5)
shot('03_after_slot')

# ④ 模式选择
click(0.513, 0.575)          # 战役
time.sleep(3.0)
shot('04_after_campaign')

# ⑤ 再按一次/回车，看是否会到关卡列表
key(0x0D)
time.sleep(3.0)
shot('05_after_enter')

# ⑥ 尝试用左右方向键在关卡列表里切换
key(0x27)
time.sleep(1.0)
shot('06_right')

subprocess.run(['taskkill', '/F', '/IM', 'AlienShooter.exe'], capture_output=True)
try: os.remove(os.path.join(UPD, 'chs_logall.txt'))
except OSError: pass
print('done')
