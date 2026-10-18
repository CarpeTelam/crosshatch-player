"""
PlatformIO pre-build script: apply the display lab's decoder patches (the x4pro env only, where the lab is built).

The gray viewer (story dlab-g3) drives PNGdec and JPEGDEC directly; the fork patches them for it, in fork-only folders,
so no other env changes (PlatformIO keeps one library copy per env):
  * scripts/lab_decoder_patches/pngdec/  -> .pio/libdeps/<env>/PNGdec
  * scripts/lab_decoder_patches/jpegdec/ -> .pio/libdeps/<env>/JPEGDEC, after upstream's scripts/jpegdec_patches
    (scripts/patch_jpegdec.py, in base's extra_scripts, which run first)
The x4pro env's own converters (covers, chapter images) use the same patched copies.

Retired patches (scripts/lab_decoder_patches/retired/<library>/, patches a copy may still carry from an earlier build
of this branch) are taken out first: each that reverses cleanly is reversed.

`git apply` is used as a plain patch tool: GIT_CEILING_DIRECTORIES is the library folder's parent, so git looks for a
repository in the library folder alone (JPEGDEC's clone has one; PNGdec, from the registry, has none) and never takes
the project's. Idempotent, as scripts/patch_jpegdec.py is:
  * `git apply --check --reverse` succeeds  -> already applied, skip
  * `git apply --check`            succeeds  -> apply
  * neither succeeds                          -> abort the build, naming the library folder to delete (PlatformIO
                                                 installs it again, unpatched, on the next build)
A missing patch folder, or a missing library folder in an env whose libdeps exist, aborts the build too; a `clean`
returns at once, and an env with no libdeps yet returns with a line saying the patches were not applied. The same patches are applied to copies in the host test's build tree by
test/game_display_lab/CMakeLists.txt.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import subprocess
import sys

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402 (SCons-provided)


LAB_PATCH_DIR = os.path.join(env["PROJECT_DIR"], "scripts", "lab_decoder_patches")  # noqa: F821
LIBRARIES = (("pngdec", "PNGdec"), ("jpegdec", "JPEGDEC"))


def patch_lab_decoders(env):
    if env.GetOption("clean") or "clean" in COMMAND_LINE_TARGETS:
        return
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps", env["PIOENV"])
    if not os.path.isdir(libdeps_dir):
        print("Lab decoder patches not applied: %s does not exist yet (lib_deps not installed)" % libdeps_dir)
        return
    for folder, library in LIBRARIES:
        lib_dir = os.path.join(libdeps_dir, library)
        if not os.path.isdir(os.path.join(lib_dir, "src")):
            _fail("the lab's %s is not at %s (lib_deps installs it before this script runs)" % (library, lib_dir))
        for patch in _patch_files(os.path.join("retired", folder), required=False):
            _retire_one(lib_dir, library, patch)
        for patch in _patch_files(folder, required=True):
            _apply_one(lib_dir, library, patch)


def _fail(message):
    sys.stderr.write("ERROR: %s\n" % message)
    raise SystemExit(1)


def _patch_files(folder, required):
    patch_dir = os.path.join(LAB_PATCH_DIR, folder)
    if not os.path.isdir(patch_dir):
        if required:
            _fail("lab decoder patches missing (expected directory %s)" % patch_dir)
        return []
    patches = sorted(
        os.path.join(patch_dir, name) for name in os.listdir(patch_dir) if name.endswith(".patch")
    )
    if required and not patches:
        _fail("lab decoder patches missing (no .patch files in %s)" % patch_dir)
    return patches


def _git_apply(lib_dir, patch_path, *args):
    environment = dict(os.environ, GIT_CEILING_DIRECTORIES=os.path.dirname(os.path.abspath(lib_dir)))
    return subprocess.run(
        ["git", "apply", *args, patch_path],
        cwd=lib_dir,
        capture_output=True,
        text=True,
        env=environment,
    )


def _retire_one(lib_dir, library, patch_path):
    name = os.path.basename(patch_path)
    if _git_apply(lib_dir, patch_path, "--check", "--reverse").returncode != 0:
        return
    reversed_ = _git_apply(lib_dir, patch_path, "--reverse")
    if reversed_.returncode != 0:
        _fail(
            "%s still carries the retired lab patch %s and it cannot be taken out:\n%s%s\n"
            "Delete %s; the next build installs it again." % (library, name, reversed_.stdout, reversed_.stderr, lib_dir)
        )
    print("Took out retired %s lab patch: %s" % (library, name))


def _apply_one(lib_dir, library, patch_path):
    name = os.path.basename(patch_path)
    if _git_apply(lib_dir, patch_path, "--check", "--reverse").returncode == 0:
        return
    check = _git_apply(lib_dir, patch_path, "--check")
    if check.returncode != 0:
        # Not applied, not appliable: the library source has diverged from what the patch expects. Don't write a
        # half-patched file.
        _fail(
            "%s lab patch %s does not apply cleanly:\n%s%s\nDelete %s; the next build installs it again, unpatched."
            % (library, name, check.stdout, check.stderr, lib_dir)
        )
    applied = _git_apply(lib_dir, patch_path)
    if applied.returncode != 0:
        _fail("%s lab patch %s failed:\n%s%s\nDelete %s." % (library, name, applied.stdout, applied.stderr, lib_dir))
    print("Applied %s lab patch: %s" % (library, name))


patch_lab_decoders(env)  # noqa: F821
