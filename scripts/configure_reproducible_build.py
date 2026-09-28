"""Set a stable compiler timestamp from the source commit."""

from __future__ import annotations

import hashlib
import os
import subprocess
from pathlib import Path


def resolve_source_date_epoch(project_dir: Path, environ: dict[str, str]) -> str:
    configured = environ.get("SOURCE_DATE_EPOCH", "").strip()
    if configured:
        if not configured.isdigit():
            raise RuntimeError("SOURCE_DATE_EPOCH must be an unsigned integer")
        return configured
    result = subprocess.run(
        ["git", "show", "-s", "--format=%ct", "HEAD"],
        cwd=project_dir,
        check=True,
        capture_output=True,
        text=True,
    )
    resolved = result.stdout.strip()
    if not resolved.isdigit():
        raise RuntimeError("Unable to resolve the source commit timestamp")
    return resolved


def resolve_source_commit(project_dir: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=project_dir,
        check=True,
        capture_output=True,
        text=True,
    )
    resolved = result.stdout.strip()
    if len(resolved) != 40 or any(character not in "0123456789abcdef" for character in resolved):
        raise RuntimeError("Unable to resolve the source commit SHA")
    return resolved


def resolve_source_dirty(project_dir: Path) -> bool:
    result = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=project_dir,
        check=True,
        capture_output=True,
        text=True,
    )
    return bool(result.stdout.strip())


def resolve_build_contract_sha256(project_dir: Path) -> str:
    path = project_dir / "config" / "firmware_build_contract.json"
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compiler_path_prefix_flags(project_dir: Path, home_dir: Path | None = None) -> list[str]:
    """Rewrite machine-specific source roots before they enter debug or __FILE__ data."""
    project = project_dir.resolve().as_posix().rstrip("/")
    home = (home_dir or Path.home()).resolve().as_posix().rstrip("/")
    mappings: dict[str, str] = {}
    for source_root in sorted({project, home}, key=len, reverse=True):
        if not source_root:
            continue
        mappings[source_root] = "."
        if source_root.startswith("/"):
            mappings[f"/{source_root}"] = "."
    return [f"-ffile-prefix-map={source_root}=." for source_root in mappings]


def configure(scons_env) -> None:
    project_dir = Path(scons_env.subst("$PROJECT_DIR"))
    environment = scons_env.subst("$PIOENV")
    profile_by_environment = {
        "m5stamp-c3u": ("demo", 1),
        "m5stamp-c3u-demo": ("demo", 1),
        "m5stamp-c3u-initial": ("initial", 2),
    }
    profile = profile_by_environment.get(environment)
    if profile is None:
        build_flags = str(scons_env.get("BUILD_FLAGS", ""))
        if "ULSA_PROFILE_DEMO" in build_flags:
            profile = ("demo", 1)
        elif "ULSA_PROFILE_INITIAL" in build_flags:
            profile = ("initial", 2)
        else:
            raise RuntimeError(f"Unsupported firmware profile environment: {environment}")
    profile_name, profile_code = profile
    epoch = resolve_source_date_epoch(project_dir, dict(os.environ))
    commit = resolve_source_commit(project_dir)
    dirty = resolve_source_dirty(project_dir)
    build_contract = resolve_build_contract_sha256(project_dir)
    scons_env.Append(CCFLAGS=compiler_path_prefix_flags(project_dir))
    process_environment = dict(scons_env.get("ENV", {}))
    process_environment["SOURCE_DATE_EPOCH"] = epoch
    scons_env["ENV"] = process_environment
    os.environ["SOURCE_DATE_EPOCH"] = epoch
    scons_env.Append(CPPDEFINES=[
        ("ULSA_SOURCE_COMMIT_SHA", f'\\"{commit}\\"'),
        ("ULSA_SOURCE_DIRTY", "1" if dirty else "0"),
        ("ULSA_FIRMWARE_PROFILE_NAME", f'\\"{profile_name}\\"'),
        ("ULSA_FIRMWARE_PROFILE_CODE", str(profile_code)),
        ("ULSA_BUILD_CONTRACT_SHA256", f'\\"{build_contract}\\"'),
    ])
    print(f"Build timestamp source: SOURCE_DATE_EPOCH={epoch}")
    print(
        f"Build source identity: {commit} profile={profile_name} "
        f"dirty={str(dirty).lower()} contract={build_contract}"
    )


if "Import" in globals():
    Import("env")
    configure(env)
