"""MonoBehaviour script GUID -> class name, and the base-class chain of project scripts.

Built-in uGUI / TextMeshPro / Unity Localization scripts live in packages (not in Assets), so their GUIDs
are fixed here; project and submodule scripts are found through their .cs.meta files, and their base
class is read from the source (`class X : Image`) so that subclasses of known components still convert."""
import os
import re

# com.unity.ugui (2.0, TextMeshPro included) and friends: stable script GUIDs.
BUILTIN = {
    'fe87c0e1cc204ed48ad3b37840f39efc': 'Image',
    '1344c3c82d62a2a41a3576d8abb8e3ea': 'RawImage',
    '5f7201a12d95ffc409449d95f23cf332': 'Text',
    'f4688fdb7df04437aeb418b961361dc5': 'TextMeshProUGUI',
    '4e29b1a8efbd4b44bb3f3716e73f07ff': 'Button',
    '9085046f02f69544eb97fd06b6048fe2': 'Toggle',
    '2fafe2cfe61f6974895a912c3755e8f1': 'ToggleGroup',
    '67db9e8f0e2ae9c40bc1e2b64352a6b4': 'Slider',
    '2a4db7a114972834c8e4117be1d82ba3': 'Scrollbar',
    '1aa08ab6e0800fa44ae55d278d1423e3': 'ScrollRect',
    '31a19414c41e5ae4aae2af33fee712f6': 'Mask',
    '3312d7739989d2b4e91e6319e9a96d76': 'RectMask2D',
    '30649d3a9faa99c48a7b1166b86bf2a0': 'HorizontalLayoutGroup',
    '59f8146938fff824cb5fd77236b75775': 'VerticalLayoutGroup',
    '8a8695521f0d02e499659fee002a26c2': 'GridLayoutGroup',
    '306cc8c2b49d7114eaa3623786fc2126': 'LayoutElement',
    '3245ec927659c4140ac4f8d17403cc18': 'ContentSizeFitter',
    '86710e43de46f6f4bac7c8e50813a599': 'AspectRatioFitter',
    '0cd44c1031e13a943bb63640046fad76': 'CanvasScaler',
    'dc42784cf147c0c48a680349fa168899': 'GraphicRaycaster',
    'cfabb0440166ab443bba8876756fdfa9': 'Shadow',
    'e19747de3f5aca642ab2be37e372fb86': 'Outline',
    'd199490a83bb2b844b9695cbf13b01ef': 'InputField',
    '2da0c512f12947e489f739169773d7ca': 'TMP_InputField',
    '0d0b652f32a2cc243917e4028fa0aeb4': 'Dropdown',
    '7b743370ac3e4ec2a1668f5455a8ef8a': 'TMP_Dropdown',
    'd0b148fe25e99eb48b9724523833bab1': 'EventTrigger',
    '76c392e42b5098c458856cdf6ecaaaa1': 'EventSystem',
    '4f231c4fb786f3946a6b90b886c48677': 'StandaloneInputModule',
    'a79441f348de89743a2939f4d699eac1': 'UniversalAdditionalCameraData',
    '474bcb49853aa07438625e644c072ee6': 'UniversalAdditionalLightData',
}

# Classes that are only plumbing for the conversion (nothing to draw or report).
SILENT = {'CanvasScaler', 'GraphicRaycaster', 'EventSystem', 'StandaloneInputModule', 'UniversalAdditionalCameraData',
          'UniversalAdditionalLightData', 'CanvasRebuilderEvents', 'TemplateMarker', 'LayoutElement', 'ToggleGroup',
          'ListRectTransformChangedInternal', 'WindowSystemBreadcrumbs', 'Coroutines', 'Tweener'}

_CLASS = re.compile(r'\bclass\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^>{]*>)?\s*:\s*([A-Za-z_][A-Za-z0-9_.]*)')


class Scripts:
    """GUID -> class name for the whole project; `chain(name)` = the class and its known base classes."""

    def __init__(self, roots):
        self.by_guid, self.src = dict(BUILTIN), {}
        seen = set()
        for root in roots:
            root = os.path.abspath(root)
            if root in seen or not os.path.isdir(root):
                continue
            seen.add(root)
            for dp, dns, fns in os.walk(root):
                dns[:] = sorted(d for d in dns if d not in ('Library', 'Temp', 'Logs', 'obj', '.git'))
                for fn in fns:
                    if not fn.endswith('.cs.meta'):
                        continue
                    try:
                        with open(os.path.join(dp, fn), 'r', encoding='utf-8', errors='replace') as f:
                            m = re.search(r'^guid:\s*([0-9a-fA-F]{32})', f.read(2048), re.M)
                    except OSError:
                        continue
                    if m:
                        name = fn[:-8]
                        self.by_guid.setdefault(m.group(1).lower(), name)
                        self.src.setdefault(name, os.path.join(dp, fn[:-5]))
        self._bases = {}

    def name(self, ref):
        g = (ref or {}).get('guid') if isinstance(ref, dict) else ref
        return self.by_guid.get(str(g or '').lower(), '')

    def base(self, name):
        if name not in self._bases:
            b = ''
            path = self.src.get(name)
            if path:
                try:
                    with open(path, 'r', encoding='utf-8', errors='replace') as f:
                        text = f.read(200000)
                    found = list(_CLASS.finditer(text))
                    hit = next((m for m in found if m.group(1) == name), found[0] if found else None)
                    if hit:   # a file whose class has another name: Unity binds the script by file, so does this
                        b = hit.group(2).split('.')[-1]
                except OSError:
                    pass
            self._bases[name] = b
        return self._bases[name]

    def chain(self, name):
        out, n = [], name
        while n and n not in out and len(out) < 12:
            out.append(n)
            n = self.base(n)
        return out
