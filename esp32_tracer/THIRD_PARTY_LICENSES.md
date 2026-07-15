# Third-Party Licenses

This directory bundles source code from two different origins with two different licenses.

## SEGGER RTT (third-party)
* Files: `SEGGER_RTT.c`, `SEGGER_RTT.h`, `SEGGER_RTT_Conf.h`
* Copyright (c) 1995 - 2018 SEGGER Microcontroller GmbH, www.segger.com
* License: modified BSD (redistribution permitted with retention of copyright notice, conditions, and disclaimer — full text is embedded in the header of each file)

These files are unmodified/vendored from SEGGER and are **not** covered by this project's own MIT license (`/LICENSE.md`). Redistribution must retain the SEGGER copyright notice already present in each file's header.

## Project code (MIT)
* Files: `freertos_hooks.h`, `task_tracer.c`, `task_tracer.h`, `CMakeLists.txt`
* Copyright (c) 2026 Marcel Gruszecki
* License: MIT, see `/LICENSE.md` at the repository root.
