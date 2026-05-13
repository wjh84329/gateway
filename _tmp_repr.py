# -*- coding: utf-8 -*-
import pathlib, re
t = pathlib.Path(r"D:/7xNew/Qynet.GamePlatform/TenantServer/Controllers/DiagnosticsController.cs").read_text(encoding="utf-8")
for pat in [r'OrderNotify JSON', r'UpdateOrderRecord', r'请']:
    pass
m = re.search(r'message = "([^"]*OrderNotify[^"]*)"', t)
print("m1", repr(m.group(1)) if m else None)
m2 = re.search(r'if \(model == null\) return BadRequest\(new \{ message = "([^"]+)" \}\);', t)
# first occurrence
idx = t.find('TestOrderNotify')
sub = t[idx:idx+400]
m3 = re.search(r'message = "([^"]+)"', sub)
s = m3.group(1) if m3 else ""
print("codepoints", [hex(ord(c)) for c in s[:20]])
