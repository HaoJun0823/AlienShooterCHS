# -*- coding: utf-8 -*-
"""进关卡 + 自动战斗 + 截图 + 保留日志。
用法: python run_battle.py <tag> [seconds]
"""
import subprocess, time, os, sys, ctypes
from ctypes import wintypes
from PIL import ImageGrab

GAME = r'I:/LocalGames/AlienShooter v1.22DIC'
OUT  = r'G:/Projects/AlienShooterCHS/archives/screenshots_tmp'
UPD  = os.path.join(GAME, 'update')
LOG  = os.path.join(UPD, 'chs_probe.log')
u32  = ctypes.windll.user32

tag = sys.argv[1] if len(sys.argv) > 1 else 'b'
SECS = int(sys.argv[2]) if len(sys.argv) > 2 else 45
os.makedirs(OUT, exist_ok=True)

# 清开关（保留 logall 可选：用环境变量 CHS_LOGALL=1 打开）
for f in os.listdir(UPD):
    if f.startswith('chs_off') or f.startswith('chs_mapdiag'):
        try: os.remove(os.path.join(UPD, f))
        except OSError: pass
    if f.startswith('chs_logall') and os.environ.get('CHS_LOGALL') != '1':
        try: os.remove(os.path.join(UPD, f))
        except OSError: pass
try: os.remove(LOG)
except OSError: pass
if os.environ.get('CHS_LOGALL') == '1':
    open(os.path.join(UPD, 'chs_logall.txt'), 'w').close()
if os.environ.get('CHS_MAPDIAG') == '1':
    open(os.path.join(UPD, 'chs_mapdiag.txt'), 'w').close()


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


proc = subprocess.Popen([os.path.join(GAME, 'AlienShooter.exe')], cwd=GAME)
time.sleep(7)
w = find_game()
if not w:
    print('NO WINDOW'); sys.exit(1)
h, l, tp, r, b = w[0]
print('window', l, tp, r, b, 'size', r-l, b-tp)
u32.ShowWindow(h, 9)
u32.SetWindowPos(h, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
time.sleep(0.6)


def shot(name):
    u32.SetForegroundWindow(h); u32.BringWindowToTop(h); time.sleep(0.9)
    im = ImageGrab.grab().crop((l, tp, r, b))
    p = os.path.join(OUT, 'b_%s_%s.png' % (tag, name))
    im.save(p)
    print('shot', name, '->', p)


def click(fx, fy):
    u32.SetForegroundWindow(h); u32.BringWindowToTop(h); time.sleep(0.4)
    x, y = int(l + (r - l) * fx), int(tp + (b - tp) * fy)
    u32.SetCursorPos(x, y); time.sleep(0.3)
    u32.mouse_event(0x0002, 0, 0, 0, 0)
    u32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.6)


def key(vk, hold=0.06):
    u32.SetForegroundWindow(h); time.sleep(0.2)
    u32.keybd_event(vk, 0, 0, 0)
    time.sleep(hold)
    u32.keybd_event(vk, 0, 2, 0)
    time.sleep(0.5)


def move_mouse(fx, fy):
    x, y = int(l + (r - l) * fx), int(tp + (b - tp) * fy)
    u32.SetCursorPos(x, y)


KEY_W, KEY_A, KEY_S, KEY_D = 0x57, 0x41, 0x53, 0x44

# ---- 走菜单进关卡（沿用 shots_menu_walk 的坐标）----
shot('00_boot')
click(0.50, 0.90)          # 启动画面继续
time.sleep(2.5)
click(0.27, 0.567)         # 新游戏
time.sleep(3.0)
click(0.40, 0.45)          # 选存档
time.sleep(2.5)
click(0.513, 0.575)        # 战役
time.sleep(3.0)
key(0x0D)                  # 回车进关卡
time.sleep(3.0)
shot('01_inlevel')

# ---- 自动战斗：按住左键 + WASD 绕圈 ----
u32.SetForegroundWindow(h)
u32.mouse_event(0x0004, 0, 0, 0, 0)
time.sleep(0.3)
move_mouse(0.5, 0.5)
u32.mouse_event(0x0002, 0, 0, 0, 0)      # 左键按下（持续射击）
print('firing...')
t0 = time.time()
i = 0
while time.time() - t0 < SECS:
    i += 1
    # 朝不同方向移动，朝屏幕中心射击
    seq = [(KEY_W, 1.2), (KEY_D, 1.0), (KEY_S, 1.2), (KEY_A, 1.0)]
    for vk, dur in seq:
        u32.keybd_event(vk, 0, 0, 0)
        # 期间抖动鼠标准星
        tt = time.time()
        while time.time() - tt < dur:
            move_mouse(0.5 + 0.12 * (1 if (int(time.time()*3) % 2) else -1),
                       0.5 + 0.12 * (1 if (int(time.time()*2) % 2) else -1))
            time.sleep(0.08)
        u32.keybd_event(vk, 0, 2, 0)
        if i % 3 == 0:
            shot('mid%02d' % i)
        if time.time() - t0 > SECS: break
u32.mouse_event(0x0004, 0, 0, 0, 0)      # 松开左键
time.sleep(1.0)
shot('02_after_battle')

subprocess.run(['taskkill', '/F', '/IM', 'AlienShooter.exe'], capture_output=True)
try: os.remove(os.path.join(UPD, 'chs_logall.txt'))
except OSError: pass

# ---- 打印关卡内日志（cjk 记录）----
print('\n===== 日志中的 cjk 记录（关卡内）=====')
try:
    txt = open(LOG, 'rb').read().decode('gbk', errors='replace')
    lines = txt.splitlines()
    cjk = [x for x in lines if '[cjk #' in x or '[map' in x or 'metr' in x]
    for x in cjk[-60:]:
        print(' ', x)
    print('\n总行数', len(lines))
except Exception as e:
    print(' 读日志失败', e)
print('done')
