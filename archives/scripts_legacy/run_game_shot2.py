# -*- coding: utf-8 -*-
import subprocess, time, os, sys, ctypes
from ctypes import wintypes
from PIL import ImageGrab
GAME = r'I:/LocalGames/AlienShooter v1.22DIC'; OUT = r'G:/Projects/AlienShooterCHS/tools_out'
UPD = os.path.join(GAME, 'update'); u32 = ctypes.windll.user32
tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
try: os.remove(os.path.join(UPD, 'chs_probe.log'))
except OSError: pass
def find_game():
    res=[]
    def cb(h,l):
        if not u32.IsWindowVisible(h): return True
        n=u32.GetWindowTextLengthW(h)
        if n:
            b=ctypes.create_unicode_buffer(n+1); u32.GetWindowTextW(h,b,n+1)
            if b.value=='AlienShooter':
                r=wintypes.RECT(); u32.GetWindowRect(h,ctypes.byref(r))
                res.append((h,r.left,r.top,r.right,r.bottom))
        return True
    u32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool,wintypes.HWND,wintypes.LPARAM)(cb),0); return res
p = subprocess.Popen([os.path.join(GAME,'AlienShooter.exe')], cwd=GAME)
time.sleep(6)
w = find_game()
h,l,tp,r,b = w[0]
u32.ShowWindow(h,9); u32.SetWindowPos(h,-1,0,0,0,0,0x0001|0x0002); time.sleep(0.5)
u32.SetForegroundWindow(h); time.sleep(0.8)
u32.SetCursorPos((l+r)//2,(tp+b)//2); time.sleep(0.3)
u32.mouse_event(0x0002,0,0,0,0); u32.mouse_event(0x0004,0,0,0,0)
time.sleep(7)
def grab(tag2, tries=6):
    best=None
    for _ in range(tries):
        u32.SetForegroundWindow(h); u32.BringWindowToTop(h); time.sleep(0.6)
        im=ImageGrab.grab().crop((l,tp,r,b))
        nz=sum(1 for v in im.convert('L').getdata() if v>12)
        if best is None or nz>best[0]: best=(nz,im)
        if nz>20000: break
        time.sleep(0.8)
    best[1].save(os.path.join(OUT,'shot_%s.png'%tag2)); print('saved',tag2,'nonblack=',best[0])
grab(tag+'a')                      # 菜单
u32.SetForegroundWindow(h); time.sleep(0.3)
u32.keybd_event(0x0D,0,0,0); u32.keybd_event(0x0D,0,2,0)   # Enter
time.sleep(9)
grab(tag+'b')                      # 进入后
subprocess.run(['taskkill','/F','/IM','AlienShooter.exe'],capture_output=True)
