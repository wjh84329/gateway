# -*- coding: utf-8 -*-
from pathlib import Path
import re

p = Path(r"D:/7xNew/Qynet.GamePlatform/TenantServer/Controllers/DiagnosticsController.cs")
t = p.read_text(encoding="utf-8")

# All string literals in message = "..." or content = "..."
msgs = set(re.findall(r'message = "([^"]*)"', t))
msgs |= set(re.findall(r'content = "([^"]*)"', t))
for m in sorted(msgs, key=lambda s: (len(s), s)):
    if any(ord(c) > 127 or c == "\ufffd" for c in m):
        print(repr(m))
