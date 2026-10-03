# -*- coding: utf-8 -*-
"""进关卡 → 移动鼠标（=移动相机）→ 逐张截图，用来验证 HUD 是否跟着相机跑"""
import subprocess, time, os, sys, ctypes
from ctypes import wintypes
from PIL import ImageGrab
GAME = r'I:/LocalGames/AlienShooter v1.22DIC'
OUT  = r'G:/Projects/AlienShooterCHS/tools_out'
UPD  = os.path.join(GAME, 'update')
u32 = ctypes.windll.user32
tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
def wpath(n): return os.path.join(UPD, n)
for f in os.listdir(UPD):
    if f.startswith('chs_logall') or f.startswith('chs_mapdiag'):
        try: os.remove(wpath(f))
        except OSError: pass
try: os.remove(wpath('chs_probe.log'))
except OSError: pass
open(wpath('chs_mapdiag.txt'), 'w').close()
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
def click(h,l,tp,r,b,fx,fy):
    u32.SetForegroundWindow(h); u32.BringWindowToTop(h); time.sleep(0.35)
    x,y = int(l+(r-l)*fx), int(tp+(b-tp)*fy)
    u32.SetCursorPos(x,y); time.sleep(0.25)
    u32.mouse_event(0x0002,0,0,0,0); u32.mouse_event(0x0004,0,0,0,0); time.sleep(0.3)
def shot(l,tp,r,b,name):
    for _ in range(4):
        u32.SetForegroundWindow(h_g); u32.BringWindowToTop(h_g); time.sleep(0.4)
        im = ImageGrab.grab().crop((l,tp,r,b))
        if sum(1 for v in im.convert('L').getdata() if v > 12) > 5000: break
        time.sleep(0.4)
    im.save(os.path.join(OUT,'m_%s_%s.png'%(tag,name))); print('shot',name)
proc = subprocess.Popen([os.path.join(GAME,'AlienShooter.exe')], cwd=GAME)
time.sleep(6)
w = find_game(); h_g,l,tp,r,b = w[0]
u32.ShowWindow(h_g,9); u32.SetWindowPos(h_g,-1,0,0,0,0,0x0001|0x0002); time.sleep(0.5)
click(h_g,l,tp,r,b,0.50,0.90)
time.sleep(2.5)
click(h_g,l,tp,r,b,0.27,0.567)     # 新游戏
time.sleep(3.0)
click(h_g,l,tp,r,b,0.513,0.575)    # 战役
time.sleep(3.0)
u32.SetForegroundWindow(h_g); time.sleep(0.2)
u32.keybd_event(0x0D,0,0,0); u32.keybd_event(0x0D,0,2,0)   # Enter 开始
time.sleep(8)
cx, cy = (l+r)//2, (tp+b)//2
# ① 鼠标居中
u32.SetCursorPos(cx, cy); time.sleep(2.0); shot(l,tp,r,b,'center')
# ② 鼠标移到左边缘（相机应向左滚）
u32.SetCursorPos(l+60, cy); time.sleep(3.0); shot(l,tp,r,b,'left')
# ③ 鼠标移到右边缘
u32.SetCursorPos(r-60, cy); time.sleep(3.0); shot(l,tp,r,b,'right')
# ④ 鼠标移到底部
u32.SetCursorPos(cx, b-40); time.sleep(3.0); shot(l,tp,r,b,'down')
subprocess.run(['taskkill','/F','/IM','AlienShooter.exe'],capture_output=True)
try: os.remove(wpath('chs_mapdiag.txt'))
except OSError: pass
print('done')
