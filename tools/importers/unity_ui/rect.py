"""RectTransform -> absolute RmlUi box. Canvas units become dp (the screen's canvas is scaled at run time
like a CanvasScaler: ugui runtime), Unity's y axis points up, RmlUi's down."""
import math

from lib.unity.unity_yaml import get, num


def v2(d, default=(0.0, 0.0)):
    if not isinstance(d, dict):
        return default
    return (num(d.get('x'), default[0]), num(d.get('y'), default[1]))


def fmt(x):
    x = round(float(x), 2)
    if x == int(x):
        x = int(x)
    return str(x)


def dp(x):
    return fmt(x) + 'dp'


def pct(x):
    return fmt(x * 100.0) + '%'


class Rect:
    def __init__(self, data):
        d = data or {}
        self.amin = v2(d.get('m_AnchorMin'))
        self.amax = v2(d.get('m_AnchorMax'))
        self.pos = v2(d.get('m_AnchoredPosition'))
        self.size = v2(d.get('m_SizeDelta'))
        self.pivot = v2(d.get('m_Pivot'), (0.5, 0.5))
        s = d.get('m_LocalScale') or {}
        self.scale = (num(s.get('x'), 1.0), num(s.get('y'), 1.0))
        q = d.get('m_LocalRotation') or {}
        qz, qw = num(q.get('z')), num(q.get('w'), 1.0)
        qx, qy = num(q.get('x')), num(q.get('y'))
        # z rotation of the quaternion (UI rotations are about z; x/y tilt is reported by the caller)
        self.rot = math.degrees(math.atan2(2.0 * (qw * qz + qx * qy), 1.0 - 2.0 * (qy * qy + qz * qz)))
        self.tilt = abs(qx) > 1e-4 or abs(qy) > 1e-4

    def stretch_x(self):
        return abs(self.amax[0] - self.amin[0]) > 1e-6

    def stretch_y(self):
        return abs(self.amax[1] - self.amin[1]) > 1e-6

    def box(self):
        """Absolute-position properties inside the parent rect."""
        st = {'position': 'absolute'}
        px, py = self.pivot
        if self.stretch_x():
            omin = self.pos[0] - px * self.size[0]
            omax = self.pos[0] + (1 - px) * self.size[0]
            st.update({'left': pct(self.amin[0]), 'right': pct(1 - self.amax[0])})
            if omin:
                st['margin-left'] = dp(omin)
            if omax:
                st['margin-right'] = dp(-omax)
        else:
            w = self.size[0]
            st.update({'left': pct(self.amin[0]), 'width': dp(w)})
            ml = self.pos[0] - px * w
            if ml:
                st['margin-left'] = dp(ml)
        if self.stretch_y():
            omin = self.pos[1] - py * self.size[1]
            omax = self.pos[1] + (1 - py) * self.size[1]
            st.update({'top': pct(1 - self.amax[1]), 'bottom': pct(self.amin[1])})
            if omax:
                st['margin-top'] = dp(-omax)
            if omin:
                st['margin-bottom'] = dp(omin)
        else:
            h = self.size[1]
            st.update({'top': pct(1 - self.amin[1]), 'height': dp(h)})
            mt = -(self.pos[1] + (1 - py) * h)
            if mt:
                st['margin-top'] = dp(mt)
        self.transform(st)
        return st

    def transform(self, st):
        parts = []
        if abs(self.rot) > 0.01:
            parts.append('rotate(%sdeg)' % fmt(-self.rot))
        if abs(self.scale[0] - 1) > 1e-4 or abs(self.scale[1] - 1) > 1e-4:
            parts.append('scale(%s, %s)' % (fmt(self.scale[0]), fmt(self.scale[1])))
        if parts:
            st['transform'] = ' '.join(parts)
            st['transform-origin'] = '%s %s' % (pct(self.pivot[0]), pct(1 - self.pivot[1]))
