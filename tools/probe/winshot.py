# PrintWindow capture of a process's main window (by pid) into a PNG.
import ctypes, ctypes.wintypes as W
from PIL import Image
u, g = ctypes.windll.user32, ctypes.windll.gdi32
u.SetProcessDPIAware()

def find_window(pid):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h): found.append(h)
        return True
    u.EnumWindows(cb, 0)
    return found[0] if found else None

def shot(pid, path):
    h = find_window(pid)
    if not h: return "no window"
    r = W.RECT(); u.GetClientRect(h, ctypes.byref(r))
    w, hgt = r.right, r.bottom
    if w < 2 or hgt < 2: return f"client {w}x{hgt}"
    hdc = u.GetDC(h); mdc = g.CreateCompatibleDC(hdc); bmp = g.CreateCompatibleBitmap(hdc, w, hgt)
    g.SelectObject(mdc, bmp)
    u.PrintWindow(h, mdc, 3)
    class BIH(ctypes.Structure):
        _fields_ = [("biSize", W.DWORD), ("biWidth", W.LONG), ("biHeight", W.LONG), ("biPlanes", W.WORD), ("biBitCount", W.WORD),
                    ("biCompression", W.DWORD), ("biSizeImage", W.DWORD), ("a", W.LONG), ("b", W.LONG), ("c", W.DWORD), ("d", W.DWORD)]
    bi = BIH(ctypes.sizeof(BIH), w, -hgt, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * hgt * 4)
    g.GetDIBits(mdc, bmp, 0, hgt, buf, ctypes.byref(bi), 0)
    g.DeleteObject(bmp); g.DeleteDC(mdc); u.ReleaseDC(h, hdc)
    Image.frombuffer("RGB", (w, hgt), buf, "raw", "BGRX", 0, 1).save(path)
    return f"{w}x{hgt}"
