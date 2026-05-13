# -*- coding: utf-8 -*-
import pathlib
lines = pathlib.Path(r"D:/7xNew/Qynet.GamePlatform/TenantServer/Controllers/DiagnosticsController.cs").read_text(encoding="utf-8").splitlines()
L = lines[140]  # line 141
start = L.index('message = "') + len('message = "')
end = L.rindex('"', start)
inner = L[start:end]
print(inner.encode('utf-8').hex())
