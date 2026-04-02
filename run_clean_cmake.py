import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
VCVARS = Path(r"C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
CMAKE = Path(r"C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")


def get_clean_base_env() -> dict[str, str]:
    clean: dict[str, tuple[str, str]] = {}

    for key, value in os.environ.items():
        lowered = key.lower()
        if lowered not in clean or key == "Path":
            clean[lowered] = (key, value)

    env = {key: value for key, value in clean.values()}
    env["Path"] = env.get("Path") or env.get("PATH") or ""
    env.pop("PATH", None)
    return env


def get_msvc_env() -> dict[str, str]:
    result = subprocess.run(
        ["cmd", "/c", f"call {VCVARS} && set"],
        capture_output=True,
        text=True,
        env=get_clean_base_env(),
        check=False,
    )

    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)

    env: dict[str, str] = {}
    for line in result.stdout.splitlines():
        if "=" not in line or line.startswith("**") or line.startswith("["):
            continue
        key, value = line.split("=", 1)
        env[key] = value

    env["Path"] = env.get("Path", "")
    env.pop("PATH", None)
    return env


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: run_clean_cmake.py <cmake args...>", file=sys.stderr)
        return 2

    cmd = [str(CMAKE), *sys.argv[1:]]
    process = subprocess.run(cmd, cwd=ROOT, env=get_msvc_env(), text=True, check=False)
    return process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
