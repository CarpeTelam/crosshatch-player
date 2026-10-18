"""
PlatformIO pre-build script: apply the fork's PNGdec patches (the x4pro env, which builds the display lab).

PNGdec comes from the PlatformIO registry, not a git checkout, so each patch in `scripts/pngdec_patches/` is applied
with `git apply` as a plain patch tool: GIT_CEILING_DIRECTORIES stops git from taking the project's own repository for
the library folder's. Idempotent, as scripts/patch_jpegdec.py is:
  * `git apply --check --reverse` succeeds  -> already applied, skip
  * `git apply --check`            succeeds  -> apply
  * neither succeeds                          -> abort the build

The patches (one file per fix, applied in lexical order; each says why in its header) are also applied by
test/game_display_lab/CMakeLists.txt to a copy of PNGdec's sources for the host decoder tests.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import subprocess
import sys


PATCH_DIR = os.path.join(env["PROJECT_DIR"], "scripts", "pngdec_patches")  # noqa: F821


def patch_pngdec(env):
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps", env["PIOENV"])
    png_dir = os.path.join(libdeps_dir, "PNGdec")
    if not os.path.isdir(os.path.join(png_dir, "src")):
        return
    for patch in _patch_files():
        _apply_one(png_dir, patch)


def _patch_files():
    patches = sorted(
        os.path.join(PATCH_DIR, name)
        for name in os.listdir(PATCH_DIR)
        if name.endswith(".patch")
    ) if os.path.isdir(PATCH_DIR) else []
    if not patches:
        raise RuntimeError(
            "PNGdec patches missing -- aborting build (no .patch files in %s)" % PATCH_DIR
        )
    return patches


def _git_apply(png_dir, patch_path, *args):
    environment = dict(os.environ, GIT_CEILING_DIRECTORIES=png_dir)
    return subprocess.run(
        ["git", "apply", *args, patch_path],
        cwd=png_dir,
        capture_output=True,
        text=True,
        env=environment,
    )


def _apply_one(png_dir, patch_path):
    name = os.path.basename(patch_path)
    if _git_apply(png_dir, patch_path, "--check", "--reverse").returncode == 0:
        return
    check = _git_apply(png_dir, patch_path, "--check")
    if check.returncode != 0:
        # Not applied, not appliable -- the libdep source has diverged from what the patch expects. Don't write a
        # half-patched file.
        sys.stderr.write(
            "ERROR: PNGdec patch %s does not apply cleanly:\n%s%s\n" % (name, check.stdout, check.stderr)
        )
        raise SystemExit(1)
    applied = _git_apply(png_dir, patch_path)
    if applied.returncode != 0:
        sys.stderr.write("ERROR: PNGdec patch %s failed:\n%s%s\n" % (name, applied.stdout, applied.stderr))
        raise SystemExit(1)
    print("Applied PNGdec patch: %s" % name)


patch_pngdec(env)  # noqa: F821
