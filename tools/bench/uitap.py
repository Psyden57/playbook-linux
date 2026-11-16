import ctypes, fcntl, os, struct, sys, time

UINPUT = "/dev/uinput"
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
UI_DEV_SETUP = 0x405C5503
UI_ABS_SETUP = 0x401C5504
UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_SET_ABSBIT = 0x40045567
UI_SET_PROPBIT = 0x4004556E

EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BTN_TOUCH = 0x14A
ABS_MT_SLOT, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID = 0x2F, 0x35, 0x36, 0x39
INPUT_PROP_DIRECT = 0x01

W, H = 1024, 600

# Usage:
#   uitap.py X Y                       tap
#   uitap.py X1 Y1 X2 Y2 [STEPS] [MS]  drag (default 30 steps, 50 ms)
args = [int(a) for a in sys.argv[1:]]
if len(args) == 2:
    mode, x1, y1, x2, y2, steps, ms = "tap", args[0], args[1], args[0], args[1], 1, 80
elif len(args) >= 4:
    mode, x1, y1, x2, y2 = "drag", args[0], args[1], args[2], args[3]
    steps = args[4] if len(args) > 4 else 30
    ms = args[5] if len(args) > 5 else 50
else:
    raise SystemExit("usage: uitap.py X Y | uitap.py X1 Y1 X2 Y2 [steps] [ms]")

fd = os.open(UINPUT, os.O_WRONLY | os.O_NONBLOCK)

def ioctl_setbit(nr, bit):
    fcntl.ioctl(fd, nr, int(bit))

ioctl_setbit(UI_SET_EVBIT, EV_SYN)
ioctl_setbit(UI_SET_EVBIT, EV_KEY)
ioctl_setbit(UI_SET_EVBIT, EV_ABS)
ioctl_setbit(UI_SET_KEYBIT, BTN_TOUCH)
ioctl_setbit(UI_SET_ABSBIT, ABS_MT_SLOT)
ioctl_setbit(UI_SET_ABSBIT, ABS_MT_TRACKING_ID)
ioctl_setbit(UI_SET_ABSBIT, ABS_MT_POSITION_X)
ioctl_setbit(UI_SET_ABSBIT, ABS_MT_POSITION_Y)
ioctl_setbit(UI_SET_PROPBIT, INPUT_PROP_DIRECT)

def abs_setup(code, lo, hi):
    fcntl.ioctl(fd, UI_ABS_SETUP, struct.pack("<HHiiiiii", code, 0, 0, lo, hi, 0, 0, 0))

abs_setup(ABS_MT_SLOT, 0, 9)
abs_setup(ABS_MT_TRACKING_ID, 0, 65535)
abs_setup(ABS_MT_POSITION_X, 0, W - 1)
abs_setup(ABS_MT_POSITION_Y, 0, H - 1)

setup = struct.pack("<HHHH80sI", 0x3, 0x1, 0x1, 0x1, b"playbook-tap", 0)
fcntl.ioctl(fd, UI_DEV_SETUP, setup)
fcntl.ioctl(fd, UI_DEV_CREATE)
print("created", flush=True)
time.sleep(2.0)

def ev(t, c, v):
    os.write(fd, struct.pack("<iiHHi", 0, 0, t, c, v))

def syn():
    ev(EV_SYN, 0, 0)

def point(x, y):
    ev(EV_ABS, ABS_MT_POSITION_X, x)
    ev(EV_ABS, ABS_MT_POSITION_Y, y)

# Down.
ev(EV_ABS, ABS_MT_SLOT, 0)
ev(EV_ABS, ABS_MT_TRACKING_ID, 1)
point(x1, y1)
ev(EV_KEY, BTN_TOUCH, 1)
syn()

if mode == "drag":
    for i in range(1, steps + 1):
        time.sleep(ms / 1000.0)
        point(x1 + (x2 - x1) * i // steps, y1 + (y2 - y1) * i // steps)
        syn()
else:
    time.sleep(ms / 1000.0)

# Up.
ev(EV_ABS, ABS_MT_TRACKING_ID, -1)
ev(EV_KEY, BTN_TOUCH, 0)
syn()
time.sleep(0.3)

fcntl.ioctl(fd, UI_DEV_DESTROY)
os.close(fd)
print("%s at %d,%d%s" % (mode, x1, y1, (" -> %d,%d" % (x2, y2)) if mode == "drag" else ""))
