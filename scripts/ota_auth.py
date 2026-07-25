Import("env")

from pathlib import Path
import re


if "upload" in COMMAND_LINE_TARGETS:
    config_path = Path(env["PROJECT_DIR"]) / "include" / "config.hpp"
    config = config_path.read_text(encoding="utf-8")
    password_match = re.search(
        r'^\s*#define\s+OTA_PASSWORD\s+"([^"]+)"\s*(?://.*)?$',
        config,
        re.MULTILINE,
    )

    if password_match is None:
        raise RuntimeError(
            "OTA is disabled. Define a nonempty password directly as "
            '#define OTA_PASSWORD "password" in include/config.hpp'
        )

    # The ESP32 platform enables espota.py's debug output by default, which
    # prints all parsed options, including the authentication password.
    env.Replace(
        UPLOADERFLAGS=[
            flag for flag in env["UPLOADERFLAGS"] if flag != "--debug"
        ]
    )
    env.Append(UPLOADERFLAGS=["--auth", password_match.group(1)])
