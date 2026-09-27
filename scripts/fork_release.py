#!/usr/bin/env python3
"""
Make a crosshatch-player fork release: the logic behind .github/workflows/crosshatch-release.yml.

A fork release is tagged <upstream X.Y.Z>-ch.<N>, for example 1.6.5-ch.7. Devices running a build with games compare
only N, so every release image must report exactly its tag, and N must never be reused. The workflow calls these
subcommands in order; each one fails the run rather than let a release go out that devices would mishandle.

  preflight       publishing needs a run dispatched from develop on develop's head or an ancestor of it, and, once
                  a -ch.N tag's commit has API_LEVEL_FROZEN true, a commit that has it true too (a dry run only warns
                  about both); fails when another active workflow triggers on `release` or builds a gh_release env
  prepare         N = 1 + the largest number after "-ch." in any tag; checks the [crosspoint] version line, rewrites
                  it to the tag in this checkout only, and finds the release envs: every <board>-gh_release env that
                  sets FREEINK_CAP_GAMES=1; reads the game API level; writes the plan (tag, commit, envs, asset names,
                  API level)
  build           `pio run -e <env>` for each release env, checking that env's firmware.bin right after its own run:
                  an application image holding its tag, the fork release URL and not upstream's, and its own board
                  tag; copies it to <dist>/crosspoint-<tag>-<board>.bin at once, because a later env's `pio run` may
                  remove all of .pio/build (PlatformIO cleans it when the project checksum changes)
  pack-games      packs every games/<id>/ with scripts/pack_game.py; no games/<id>/ is valid
  notes           release notes naming the game API level, with each asset's SHA-256 and each package hash
  recheck         before publishing: no tag has taken N since the plan was made
  expected-assets the assets the release must hold, one "<name><TAB><size>" per line

The script comes from the workflow's commit, but the data describing the firmware comes from the commit it releases
(--project-dir, --repo-dir): the tag grammar, the fork and upstream release URLs, and the asset-name capacity and
vectors from its test/game_core/fork_version_vectors.json, the file the firmware's host tests also read (--vectors
overrides it), and the game API level from its lib/GameCore/ApiLevel.h. A commit without ApiLevel.h is released with
"No game API" in its notes, and one whose vectors have no asset_name_capacity with the 48 bytes its firmware has.

Games: `python3 scripts/pack_game.py games/<id> <out-dir>` must exit 0, write <out-dir>/<id>.cpgame, print the
package hash (16 lowercase hex digits) as its last line of output, and leave games/ unchanged.

Exit 0: passed. 1: a release rule is broken. 2: the step could not run (missing file, git or pio failure).

Local run (the rewrite changes platformio.ini; restore it with `git checkout -- platformio.ini`). PlatformIO also
reads the untracked platformio.local.ini, which CI does not have, so a local run can differ from CI's:
    printf '1.6.5-ch.3\\n' > /tmp/tags.txt
    python3 scripts/fork_release.py prepare --project-dir . --tags /tmp/tags.txt --plan /tmp/release/plan.json
    python3 scripts/fork_release.py build --plan /tmp/release/plan.json --project-dir . --dist /tmp/release/dist
    python3 scripts/fork_release_test.py      # the script's own tests; need only Python and git
"""

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import fork_common
from fork_common import API_LEVEL_HEADER, Failure, SetupError

# Relative to the checkout of the commit to release.
VECTORS_PATH = pathlib.PurePosixPath('test/game_core/fork_version_vectors.json')

DEVELOP_BRANCH_REF = 'refs/heads/develop'
RELEASE_ENV_SUFFIX = '-gh_release'
GAMES_FLAG = fork_common.GAMES_BUILD_FLAG
VERSION_DEFINE = 'CROSSPOINT_VERSION'
# A second source for the version or the flags could override the rewritten line.
BUILD_OVERRIDES = ('PLATFORMIO_BUILD_FLAGS', 'PLATFORMIO_BUILD_UNFLAGS', 'PLATFORMIO_SRC_BUILD_FLAGS')
UPSTREAM_VERSION = re.compile(r'[0-9]+\.[0-9]+\.[0-9]+')
BUILD_NUMBER_IN_TAG = re.compile(r'-ch\.([0-9]+)')
# Commits from before the vectors' asset_name_capacity field (the -ch.1 and -ch.2 releases) have firmware whose
# asset-name buffer is 48 bytes, NUL included.
LEGACY_ASSET_NAME_CAPACITY = 48

# An ESP-IDF application image, as the update path writes it into an OTA slot: the image header's magic byte, and the
# esp_app_desc_t magic word 0xABCD5432 (little-endian) right after the image and first segment headers. A merged
# image with the bootloader in front has neither at these offsets.
APP_IMAGE_MAGIC = 0xE9
APP_DESC_OFFSET = 32
APP_DESC_MAGIC = bytes.fromhex('3254cdab')
BOARD_TAG_MAGIC = b'CROSSPOINT-BOARD-V1:'
PACKAGE_HASH = re.compile(r'[0-9a-f]{16}')
# A game's directory is named by its manifest id, which is also its asset name.
GAME_ID = re.compile(r'[a-z0-9][a-z0-9-]{0,31}')

WORKFLOW_SUFFIXES = ('.yml', '.yaml')
TOP_LEVEL_ON = re.compile(r'''^(?:on|"on"|'on')\s*:(.*)$''')
TRIGGER_NAME = re.compile(r'''^["']?([A-Za-z_][\w-]*)["']?\s*(?::|$)''')


# --- Shared rules -------------------------------------------------------------------------------------------------


class Rules:
    def __init__(self, data):
        try:
            self.grammar = re.compile(data['tag_grammar'])
            self.max_tag_length = int(data['max_tag_length'])
            self.release_url = str(data['release_url'])
            self.upstream_fragment = str(data['upstream_release_url_fragment'])
            self.asset_vectors = list(data['asset_names'])
            capacity = data.get('asset_name_capacity', LEGACY_ASSET_NAME_CAPACITY)
        except (KeyError, TypeError, ValueError, re.error) as exc:
            raise SetupError(f'the version vector file is missing or has a bad field ({exc})')
        if not self.release_url or not self.upstream_fragment or not self.asset_vectors:
            raise SetupError('the version vector file has an empty field')
        # bool is an int in Python; the firmware's constant is a positive byte count.
        if type(capacity) is not int or capacity <= 0:
            raise SetupError(f'the version vector file has a bad asset_name_capacity ({capacity!r})')
        self.asset_name_capacity = capacity

    def is_tag(self, tag):
        return len(tag) <= self.max_tag_length and self.grammar.fullmatch(tag) is not None

    def asset_name(self, tag, board):
        """crosspoint-<tag>-<board>.bin, or '' when the device could not form that name."""
        if not self.is_tag(tag) or not board:
            return ''
        name = f'crosspoint-{tag}-{board}.bin'
        return name if len(name) < self.asset_name_capacity else ''

    def check_asset_vectors(self):
        for vector in self.asset_vectors:
            got = self.asset_name(vector['tag'], vector['board'])
            if got != vector['asset']:
                raise SetupError(
                    f'asset name for {vector["tag"]!r}/{vector["board"]!r} is {got!r}, the vectors say '
                    f'{vector["asset"]!r}; this script and the firmware disagree'
                )


def load_rules(path):
    try:
        data = json.loads(pathlib.Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise SetupError(f'cannot read the version vector file {path} ({exc})')
    return Rules(data)


def release_rules(args):
    """The rules of the commit to release: --vectors, or else its checkout's vector file."""
    return load_rules(args.vectors or pathlib.Path(args.project_dir) / VECTORS_PATH)


def read_api_level(checkout):
    """The ApiLevel in the checkout's ApiLevel.h, or None when it has no such file."""
    path = pathlib.Path(checkout) / API_LEVEL_HEADER
    try:
        text = path.read_text(encoding='utf-8', errors='replace')
    except FileNotFoundError:
        return None
    except OSError as exc:
        raise SetupError(f'cannot read {path} ({exc})')
    return fork_common.parse_api_level(text)


def api_title(api):
    """'Game API <n>', with ' (preview)' until the level is frozen, from a plan's 'api' value."""
    if api is None:
        return 'No game API'
    return f'Game API {api["level"]}' + ('' if api['frozen'] else ' (preview)')


def read_lines(path):
    try:
        text = pathlib.Path(path).read_text(encoding='utf-8')
    except OSError as exc:
        raise SetupError(f'cannot read {path} ({exc})')
    return [line.strip() for line in text.splitlines() if line.strip()]


def load_plan(path):
    try:
        return json.loads(pathlib.Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise SetupError(f'cannot read the plan {path} ({exc}); run "prepare" first')


def save_plan(path, plan):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')


def write_outputs(values):
    output = os.environ.get('GITHUB_OUTPUT')
    if output:
        with open(output, 'a', encoding='utf-8') as out:
            for key, value in values.items():
                out.write(f'{key}={value}\n')


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


# --- preflight ----------------------------------------------------------------------------------------------------


def ref_problems(repo_dir, commit, develop_ref, dispatch_ref):
    problems = []
    if dispatch_ref != DEVELOP_BRANCH_REF:
        problems.append(f'dispatched from {dispatch_ref or "an unknown ref"}, not {DEVELOP_BRANCH_REF}')
    code, _ = fork_common.git('merge-base', '--is-ancestor', commit, develop_ref, cwd=repo_dir, ok_codes=(0, 1))
    if code == 1:
        problems.append(f'{commit} is not {develop_ref} or an ancestor of it')
    return problems


def workflow_path(workflow_ref, repository):
    """'.github/workflows/x.yml' from GITHUB_WORKFLOW_REF ('owner/repo/.github/workflows/x.yml@refs/heads/b')."""
    prefix = repository + '/'
    if not repository or not workflow_ref.startswith(prefix) or '@' not in workflow_ref:
        raise SetupError(f'cannot find this workflow\'s path in {workflow_ref!r}')
    return workflow_ref[len(prefix) :].split('@', 1)[0]


def parse_states(lines):
    """{path: state} from 'path<TAB>state' lines, as the Actions workflow list gives them."""
    states = {}
    for line in lines:
        path, _, state = line.partition('\t')
        states[path.strip()] = state.strip()
    return states


def strip_comment(line):
    if line.lstrip().startswith('#'):
        return ''
    return re.sub(r'\s+#.*$', '', line).rstrip()


def top_level_triggers(text):
    """Event names directly under the top-level `on:` key, or None when there is no such key."""
    lines = [strip_comment(line) for line in text.splitlines()]
    for index, line in enumerate(lines):
        match = TOP_LEVEL_ON.match(line)
        if not match:
            continue
        block = []
        for child in lines[index + 1 :]:
            # A block sequence may sit at the same indent as its key; any other unindented line ends the value.
            if child.strip() and not child.startswith((' ', '-')):
                break
            if child.strip():
                block.append(child)
        inline = match.group(1).strip()
        if inline or (block and block[0].lstrip().startswith(('[', '{'))):
            # A flow value (`on: release`, `on: [push, release]`, `on: {release: ...}`, possibly over several
            # lines): every name in it counts, which errs towards reporting a workflow.
            return re.findall(r'[A-Za-z_][\w-]*', ' '.join([inline] + block))
        triggers = []
        child_indent = None
        for child in block:
            indent = len(child) - len(child.lstrip(' '))
            if child_indent is None:
                child_indent = indent
            if indent != child_indent:
                continue
            item = child.strip()
            if item.startswith('-'):
                item = item[1:].strip()
            name = TRIGGER_NAME.match(item)
            if name:
                triggers.append(name.group(1))
        return triggers
    return None


def workflow_conflicts(text):
    """Why a workflow could publish or overwrite fork release assets; empty when it cannot."""
    reasons = []
    triggers = top_level_triggers(text)
    if triggers is None:
        reasons.append('has no top-level `on:` that could be read')
    elif 'release' in triggers:
        reasons.append('triggers on `release`')
    if 'gh_release' in text:
        reasons.append('mentions a gh_release env')
    return reasons


def workflow_files(checkouts):
    """{'.github/workflows/<name>': [text, ...]} from each checkout, one text per checkout holding the file."""
    files = {}
    for checkout in checkouts:
        directory = pathlib.Path(checkout) / '.github' / 'workflows'
        if not directory.is_dir():
            continue
        for path in sorted(directory.iterdir()):
            if path.suffix in WORKFLOW_SUFFIXES and path.is_file():
                text = path.read_text(encoding='utf-8', errors='replace')
                files.setdefault(f'.github/workflows/{path.name}', []).append(text)
    return files


def workflow_problems(files, states, self_path):
    problems = []
    for path, texts in sorted(files.items()):
        # A file the API does not list may still run, so only a listed non-active state excuses it.
        if path == self_path or states.get(path, 'active') != 'active':
            continue
        reasons = []
        for text in texts:
            reasons += [reason for reason in workflow_conflicts(text) if reason not in reasons]
        if reasons:
            problems.append(f'{path} is active and {", and ".join(reasons)}; disable it in the Actions tab')
    return problems


def tag_api_level(repo_dir, tag):
    """The ApiLevel at a tag's commit, or None when that commit has no ApiLevel.h."""
    ref = f'refs/tags/{tag}'
    if not fork_common.git_text('ls-tree', '--name-only', ref, '--', API_LEVEL_HEADER, cwd=repo_dir):
        return None
    text = fork_common.git('show', f'{ref}:{API_LEVEL_HEADER}', cwd=repo_dir)[1].decode('utf-8', errors='replace')
    try:
        return fork_common.parse_api_level(text)
    except SetupError as exc:
        raise SetupError(f'at tag {tag}: {exc}')


def freeze_problems(repo_dir):
    """After the freezing release (the first from a commit with API_LEVEL_FROZEN true), every release is frozen.

    A frozen release is a -ch.N tag whose commit has API_LEVEL_FROZEN true; the tags come from the checkout, which the
    workflow fetches with full history.
    """
    level = read_api_level(repo_dir)
    if level is not None and level.frozen:
        return []
    tags = fork_common.git_text('tag', '--list', cwd=repo_dir).splitlines()
    for tag in sorted(tag for tag in tags if BUILD_NUMBER_IN_TAG.search(tag)):
        released = tag_api_level(repo_dir, tag)
        if released is not None and released.frozen:
            state = 'has no ApiLevel.h' if level is None else f'has API level {level.level} as a preview'
            return [
                f'the commit to release {state}, but {tag} released frozen API level {released.level}; after the '
                'freeze every release needs API_LEVEL_FROZEN true'
            ]
    return []


def preflight(args):
    if not args.checkout:
        raise SetupError('no --checkout to read workflows from')
    problems = []
    commit = fork_common.git_text('rev-parse', '--verify', 'HEAD^{commit}', cwd=args.repo_dir)
    rule_problems = ref_problems(args.repo_dir, commit, args.develop_ref, args.dispatch_ref)
    rule_problems += freeze_problems(args.repo_dir)
    for problem in rule_problems:
        if args.dry_run == 'true':
            print(f'warning: {problem}; a publishing run would stop here')
        else:
            problems.append(problem)
    states = parse_states(read_lines(args.workflow_states)) if args.workflow_states else {}
    files = workflow_files(args.checkout)
    if not files:
        raise SetupError('found no workflow files; is the checkout there?')
    problems += workflow_problems(files, states, workflow_path(args.workflow_ref, args.repository))
    if problems:
        raise Failure('\n'.join(problems))
    print(f'Preflight passed: checked {len(files)} workflow files.')


# --- prepare ------------------------------------------------------------------------------------------------------


def next_build_number(tags):
    highest = 0
    for tag in tags:
        for match in BUILD_NUMBER_IN_TAG.finditer(tag):
            highest = max(highest, int(match.group(1)))
    return highest + 1


def find_version_line(text):
    """(line index, value) of the one `version =` line in [crosspoint]."""
    found = []
    section = None
    for index, line in enumerate(text.splitlines()):
        header = re.match(r'\[([^\]]*)\]', line)
        if header:
            section = header.group(1).strip()
            continue
        if section == 'crosspoint':
            match = re.match(r'version\s*=\s*(.*?)\s*$', line)
            if match:
                found.append((index, match.group(1)))
    if len(found) != 1:
        raise Failure(f'platformio.ini has {len(found)} `version =` lines in [crosspoint]; expected exactly one')
    index, value = found[0]
    if not UPSTREAM_VERSION.fullmatch(value):
        raise Failure(f'the [crosspoint] version is {value!r}; expected X.Y.Z')
    return index, value


def rewrite_version(text, index, tag):
    lines = text.splitlines(keepends=True)
    ending = lines[index][len(lines[index].rstrip('\r\n')) :]
    lines[index] = f'version = {tag}{ending}'
    return ''.join(lines)


def pio_config(project_dir):
    try:
        result = subprocess.run(
            ['pio', 'project', 'config', '--json-output'], cwd=project_dir, capture_output=True, text=True
        )
    except OSError as exc:
        raise SetupError(f'cannot run pio: {exc}')
    if result.returncode != 0:
        raise SetupError(f'pio project config failed ({result.returncode}): {result.stderr.strip()}')
    try:
        return {section: dict(options) for section, options in json.loads(result.stdout)}
    except (ValueError, TypeError) as exc:
        raise SetupError(f'cannot read pio project config output ({exc})')


def flag_tokens(value):
    items = value if isinstance(value, list) else [value]
    return [token for item in items for token in str(item).split()]


def configured_version(config):
    return str(config.get('crosspoint', {}).get('version', ''))


def release_envs(config, tag, rules):
    """The release envs as [{'env', 'board', 'asset'}], checked to report exactly the tag."""
    envs = []
    problems = []
    expected_define = f'-D{VERSION_DEFINE}=\\"{tag}\\"'
    for section, options in sorted(config.items()):
        name = section[len('env:') :] if section.startswith('env:') else ''
        if not name.endswith(RELEASE_ENV_SUFFIX):
            continue
        tokens = flag_tokens(options.get('build_flags', []))
        if GAMES_FLAG not in tokens:
            continue
        board = name[: -len(RELEASE_ENV_SUFFIX)]
        defines = [token for token in tokens if VERSION_DEFINE in token]
        if defines != [expected_define]:
            problems.append(f'{name} sets {defines or "no " + VERSION_DEFINE}; expected only {expected_define}')
        asset = rules.asset_name(tag, board)
        if not asset:
            problems.append(f'{name} has no valid asset name for board {board!r}')
        envs.append({'env': name, 'board': board, 'asset': asset})
    if not envs:
        problems.append(f'no *{RELEASE_ENV_SUFFIX} env sets {GAMES_FLAG}')
    if problems:
        raise Failure('\n'.join(problems))
    return envs


def check_overrides(environ):
    set_vars = [name for name in BUILD_OVERRIDES if environ.get(name)]
    if set_vars:
        raise Failure(f'{", ".join(set_vars)} set; a release build takes its flags only from platformio.ini')


def prepare(args):
    rules = release_rules(args)
    rules.check_asset_vectors()
    project_dir = pathlib.Path(args.project_dir)
    check_overrides(os.environ)
    api = read_api_level(project_dir)

    tags = read_lines(args.tags)
    build_number = next_build_number(tags)

    ini_path = project_dir / 'platformio.ini'
    try:
        text = ini_path.read_text(encoding='utf-8')
    except OSError as exc:
        raise SetupError(f'cannot read {ini_path} ({exc})')
    index, base_version = find_version_line(text)
    before = configured_version(pio_config(project_dir))
    if before != base_version:
        raise Failure(f'PlatformIO reads the version as {before!r}, platformio.ini says {base_version!r}')

    tag = f'{base_version}-ch.{build_number}'
    if not rules.is_tag(tag):
        raise Failure(f'{tag!r} is not a valid fork tag (grammar, or longer than {rules.max_tag_length})')
    if tag in tags:
        raise Failure(f'tag {tag} already exists')

    commit = fork_common.git_text('rev-parse', '--verify', 'HEAD^{commit}', cwd=project_dir)

    ini_path.write_text(rewrite_version(text, index, tag), encoding='utf-8')
    print(f'Rewrote [crosspoint] version {base_version} -> {tag} in {ini_path} (this checkout only)')
    config = pio_config(project_dir)
    if configured_version(config) != tag:
        raise Failure(f'after the rewrite PlatformIO reads the version as {configured_version(config)!r}')
    envs = release_envs(config, tag, rules)

    plan = {
        'tag': tag,
        'build_number': build_number,
        'base_version': base_version,
        'commit': commit,
        'envs': envs,
        'api': None if api is None else api._asdict(),
        'firmware': [],
        'packages': [],
    }
    save_plan(args.plan, plan)
    env_names = ' '.join(env['env'] for env in envs)
    print(f'Release {tag} from {plan["commit"]}: {env_names}; {api_title(plan["api"])}')
    write_outputs({'tag': tag, 'commit': plan['commit'], 'envs': env_names})


# --- build and image checks ---------------------------------------------------------------------------------------


def build(args):
    rules = release_rules(args)
    plan = load_plan(args.plan)
    check_overrides(os.environ)
    dist = pathlib.Path(args.dist)
    dist.mkdir(parents=True, exist_ok=True)
    problems = []
    firmware = []
    for env in plan['envs']:
        image = pathlib.Path(args.project_dir) / '.pio' / 'build' / env['env'] / 'firmware.bin'
        # An image left by an earlier run must never stand in for this one.
        image.unlink(missing_ok=True)
        print(f'$ pio run -e {env["env"]}', flush=True)
        try:
            code = subprocess.run(['pio', 'run', '-e', env['env']], cwd=args.project_dir).returncode
        except OSError as exc:
            raise SetupError(f'cannot run pio: {exc}')
        if code != 0:
            raise SetupError(f'pio run -e {env["env"]} failed ({code})')
        # Checked and copied now: the next env's `pio run` removes all of .pio/build when the project checksum has
        # changed, for example after this build generated headers on a fresh tree (retro AI-3).
        try:
            data = image.read_bytes()
        except OSError as exc:
            raise SetupError(f'pio run -e {env["env"]} left no image at {image} ({exc})')
        found = image_problems(data, plan['tag'], env['board'], rules)
        problems += [f'{env["env"]} {problem}' for problem in found]
        if not found:
            target = dist / env['asset']
            target.write_bytes(data)
            firmware.append({'asset': env['asset'], 'size': len(data), 'sha256': sha256_file(target)})
            print(f'{env["env"]}: {env["asset"]} ({len(data):,} bytes) passed', flush=True)
    if problems:
        raise Failure('\n'.join(problems))
    plan['firmware'] = firmware
    save_plan(args.plan, plan)


def image_problems(data, tag, board, rules):
    problems = []
    if len(data) < APP_DESC_OFFSET + len(APP_DESC_MAGIC) or data[0] != APP_IMAGE_MAGIC:
        problems.append('is not an ESP application image')
    elif data[APP_DESC_OFFSET : APP_DESC_OFFSET + len(APP_DESC_MAGIC)] != APP_DESC_MAGIC:
        problems.append('has no application descriptor at its start (a merged image, not the app image?)')
    # The version string may share storage with a longer string that ends in it, so only a digit, letter, or dot
    # before it (a different version) disqualifies a match.
    if not re.search(rb'(?<![0-9A-Za-z.])' + re.escape(tag.encode()) + rb'\x00', data):
        problems.append(f'does not report its tag {tag}')
    if rules.release_url.encode() + b'\x00' not in data:
        problems.append(f'does not contain the fork release URL {rules.release_url}')
    if rules.upstream_fragment.encode() in data:
        problems.append(f'contains upstream\'s release URL ({rules.upstream_fragment})')
    count = data.count(BOARD_TAG_MAGIC)
    if count != 1:
        problems.append(f'has {count} board tags, expected 1')
    else:
        start = data.index(BOARD_TAG_MAGIC) + len(BOARD_TAG_MAGIC)
        end = data.find(b';', start, start + 32)
        named = data[start:end].decode('ascii', 'replace') if end != -1 else '?'
        if named != board:
            problems.append(f'is tagged for board {named!r}, its asset says {board!r}')
    return problems


# --- games --------------------------------------------------------------------------------------------------------


def tree_digest(directory):
    digest = hashlib.sha256()
    for path in sorted(p for p in pathlib.Path(directory).rglob('*') if p.is_file() or p.is_symlink()):
        digest.update(str(path.relative_to(directory)).encode('utf-8', 'surrogateescape') + b'\x00')
        digest.update(os.readlink(path).encode() if path.is_symlink() else path.read_bytes())
        digest.update(b'\x00')
    return digest.hexdigest()


def pack_one(project_dir, packer, game_id, out_dir):
    """(package path, package hash) or raise Failure."""
    try:
        result = subprocess.run(
            [sys.executable, str(packer), f'games/{game_id}', str(out_dir)],
            cwd=project_dir,
            stdout=subprocess.PIPE,
            text=True,
        )
    except OSError as exc:
        raise SetupError(f'cannot run {packer}: {exc}')
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        raise Failure(f'games/{game_id} failed to pack ({result.returncode})')
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    package_hash = lines[-1] if lines else ''
    if not PACKAGE_HASH.fullmatch(package_hash):
        raise Failure(f'games/{game_id}: the packer\'s last line {package_hash!r} is not a package hash')
    package = pathlib.Path(out_dir) / f'{game_id}.cpgame'
    if not package.is_file():
        raise Failure(f'games/{game_id}: the packer wrote no {package.name}')
    return package, package_hash


def pack_games(args):
    plan = load_plan(args.plan)
    project_dir = pathlib.Path(args.project_dir)
    games_dir = project_dir / 'games'
    game_ids = sorted(p.name for p in games_dir.iterdir() if p.is_dir()) if games_dir.is_dir() else []
    if not game_ids:
        print('No games/<id>/ directories; nothing to pack.')
        plan['packages'] = []
        save_plan(args.plan, plan)
        return
    bad_ids = [game_id for game_id in game_ids if not GAME_ID.fullmatch(game_id)]
    if bad_ids:
        raise Failure(f'games/ has directories that are not game ids ({GAME_ID.pattern}): {", ".join(bad_ids)}')
    packer = project_dir / 'scripts' / 'pack_game.py'
    if not packer.is_file():
        raise Failure(f'games/ has {", ".join(game_ids)} but scripts/pack_game.py is missing')

    dist = pathlib.Path(args.dist)
    dist.mkdir(parents=True, exist_ok=True)
    before = tree_digest(games_dir)
    packages = []
    problems = []
    with tempfile.TemporaryDirectory() as staging:
        for game_id in game_ids:
            out_dir = pathlib.Path(staging) / game_id
            out_dir.mkdir()
            try:
                package, package_hash = pack_one(project_dir, packer, game_id, out_dir)
            except Failure as exc:
                problems.append(str(exc))
                continue
            target = dist / package.name
            shutil.copyfile(package, target)
            packages.append(
                {
                    'id': game_id,
                    'asset': package.name,
                    'package_hash': package_hash,
                    'size': target.stat().st_size,
                    'sha256': sha256_file(target),
                }
            )
            print(f'games/{game_id}: {package.name} package hash {package_hash}')
    if tree_digest(games_dir) != before:
        problems.append('packing changed files under games/; a release packs the sources as committed')
    if problems:
        raise Failure('\n'.join(problems))
    plan['packages'] = packages
    save_plan(args.plan, plan)


# --- notes and publishing -----------------------------------------------------------------------------------------


def render_notes(plan):
    api = plan['api']
    lines = [f'{plan["tag"]} · {api_title(api)}', '']
    if api is not None and not api['frozen']:
        lines += [f'Game API {api["level"]} is a preview: a game written for it may need changes to run on a later '
                  'release.', '']
    lines += [
        f'Fork release {plan["tag"]}: build {plan["build_number"]} on CrossPoint Reader {plan["base_version"]}, '
        f'from commit {plan["commit"]}.',
        '',
        'Devices running a crosshatch-player build with games are offered this release over the air. From any '
        'other firmware, install the image for your board from the SD card.',
        '',
        '## Firmware',
        '',
        '| Asset | Bytes | SHA-256 |',
        '| --- | ---: | --- |',
    ]
    lines += [f'| `{f["asset"]}` | {f["size"]:,} | `{f["sha256"]}` |' for f in plan['firmware']]
    lines += ['', '## Games', '']
    if plan['packages']:
        lines += ['| Package | Package hash | SHA-256 |', '| --- | --- | --- |']
        lines += [f'| `{p["asset"]}` | `{p["package_hash"]}` | `{p["sha256"]}` |' for p in plan['packages']]
    else:
        lines.append('No first-party games in this release.')
    return '\n'.join(lines) + '\n'


def notes(args):
    plan = load_plan(args.plan)
    if len(plan.get('firmware', [])) != len(plan.get('envs', [])) or not plan.get('firmware'):
        raise SetupError('the plan has unchecked firmware; run "build" first')
    text = render_notes(plan)
    pathlib.Path(args.out).write_text(text, encoding='utf-8')
    print(text)
    fork_common.write_step_summary(f'## Fork release {plan["tag"]}\n\n{text}\n')


def expected_assets(plan):
    """'<name>\\t<size>' for every asset, sorted, as the publish job compares them with the uploaded ones."""
    return sorted(f'{a["asset"]}\t{a["size"]}' for a in plan['firmware'] + plan['packages'])


def recheck(args):
    plan = load_plan(args.plan)
    tags = read_lines(args.tags)
    if plan['tag'] in tags or next_build_number(tags) != plan['build_number']:
        raise Failure(f'a tag has taken build number {plan["build_number"]} since the build; run the workflow again')
    if not plan.get('firmware'):
        raise Failure('the plan lists no checked firmware')
    write_outputs({'tag': plan['tag'], 'commit': plan['commit']})
    print(f'{plan["tag"]} is still free')


# --- entry point --------------------------------------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--vectors', help=f'the version vector file (default: <project-dir>/{VECTORS_PATH})')
    sub = parser.add_subparsers(dest='command', required=True)

    p = sub.add_parser('preflight', help='check the ref and the other workflows')
    p.add_argument('--repo-dir', required=True, help='checkout of the commit to release')
    p.add_argument('--develop-ref', default='origin/develop')
    p.add_argument('--dispatch-ref', default='', help='GITHUB_REF of the run')
    p.add_argument('--dry-run', choices=('true', 'false'), required=True)
    p.add_argument('--workflow-states', help='file of "path<TAB>state" lines from the Actions API')
    p.add_argument('--workflow-ref', required=True, help='GITHUB_WORKFLOW_REF of the run')
    p.add_argument('--repository', required=True, help='GITHUB_REPOSITORY')
    p.add_argument('--checkout', action='append', default=[], help='a checkout whose workflows to read (repeat)')

    p = sub.add_parser('prepare', help='number the release, rewrite the version, find the envs')
    p.add_argument('--project-dir', required=True)
    p.add_argument('--tags', required=True, help='file with every tag name, one per line')
    p.add_argument('--plan', required=True)

    p = sub.add_parser('build', help='build the release envs, checking each image and naming it as an asset')
    p.add_argument('--plan', required=True)
    p.add_argument('--project-dir', required=True)
    p.add_argument('--dist', required=True)

    p = sub.add_parser('pack-games', help='pack every games/<id>/')
    p.add_argument('--plan', required=True)
    p.add_argument('--project-dir', required=True)
    p.add_argument('--dist', required=True)

    p = sub.add_parser('notes', help='write the release notes')
    p.add_argument('--plan', required=True)
    p.add_argument('--out', required=True)

    p = sub.add_parser('recheck', help='check that the planned tag is still the next one')
    p.add_argument('--plan', required=True)
    p.add_argument('--tags', required=True)

    p = sub.add_parser('expected-assets', help='print the assets and their sizes, one per line')
    p.add_argument('--plan', required=True)

    args = parser.parse_args(argv)
    commands = {
        'preflight': preflight,
        'prepare': prepare,
        'build': build,
        'pack-games': pack_games,
        'notes': notes,
        'recheck': recheck,
        'expected-assets': lambda a: print('\n'.join(expected_assets(load_plan(a.plan)))),
    }

    def step():
        # A command passes by returning; what it returns is not an exit code.
        commands[args.command](args)

    try:
        return fork_common.exit_code(step)
    except (OSError, KeyError, TypeError) as exc:
        print(f'error: could not run: {exc!r}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
