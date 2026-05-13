# -*- coding: utf-8 -*-
p = r"D:/7xNew/Qynet.GamePlatform/TenantServer/Controllers/DiagnosticsController.cs"
b = open(p, "rb").read()
needle = b'success = false, message = "'
idx = 0
while True:
    i = b.find(needle, idx)
    if i < 0:
        break
    j = b.find(b'"', i + len(needle))
    if j < 0:
        break
    frag = b[i + len(needle) : j]
    print(frag[:120])
    idx = j + 1
