# -*- coding: utf-8 -*-
import pathlib
lines = pathlib.Path(r"D:/7xNew/Qynet.GamePlatform/TenantServer/Controllers/DiagnosticsController.cs").read_text(encoding="utf-8").splitlines()
for i, L in enumerate(lines, 1):
    if i in (141, 146, 150):
        print(i, repr(L))
