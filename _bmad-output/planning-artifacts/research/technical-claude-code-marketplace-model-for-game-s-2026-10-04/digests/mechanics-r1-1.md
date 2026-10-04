# Digest: mechanics r1

Dimension D1: Claude Code plugin marketplace mechanics. All doc pages were fetched as raw Markdown from `code.claude.com/docs/en/...md` on 2026-10-04. They are live docs and show no last-updated date. The docs now live under `/docs/en/plugins/<page>`; the old `/docs/en/plugin-marketplaces` URL serves the "Create a marketplace" page. Current Claude Code at access time: v2.1.289, published to npm 2026-10-03.

Source keys used below:
- [MR] https://code.claude.com/docs/en/plugins/marketplace-reference (Anthropic, live doc)
- [CM] https://code.claude.com/docs/en/plugin-marketplaces ("Create a marketplace"; Anthropic, live doc)
- [MF] https://code.claude.com/docs/en/plugins/manifest-reference (Anthropic, live doc)
- [LD] https://code.claude.com/docs/en/plugins/loading (Anthropic, live doc)
- [HM] https://code.claude.com/docs/en/plugins/host-marketplace (Anthropic, live doc)
- [IN] https://code.claude.com/docs/en/plugins/install (Anthropic, live doc)
- [SE] https://code.claude.com/docs/en/plugins/security (Anthropic, live doc)
- [OR] https://code.claude.com/docs/en/plugins/org (Anthropic, live doc)
- [AM] https://code.claude.com/docs/en/plugins/anthropic-marketplaces (Anthropic, live doc)
- [PB] https://code.claude.com/docs/en/plugins/publish (Anthropic, live doc)
- [CL] https://raw.githubusercontent.com/anthropics/claude-code/main/CHANGELOG.md (Anthropic; version headers carry no dates, so dates come from [NPM])
- [NPM] https://registry.npmjs.org/@anthropic-ai/claude-code (`time` field, for release dates)
- [OFF] https://raw.githubusercontent.com/anthropics/claude-plugins-official/main/.claude-plugin/marketplace.json (Anthropic; live file)

## Findings

### 1. Marketplace manifest (`marketplace.json`)
- claim: The catalog lives at `<root>/.claude-plugin/marketplace.json`. Relative plugin sources resolve from the marketplace root, which is the directory that contains `.claude-plugin/`, not from `.claude-plugin/` itself. A catalog stored anywhere else must be declared in `extraKnownMarketplaces` with `path`, because `marketplace add` has no option for it. | source: [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: The top-level required fields are `name`, `owner` (`owner.name` required; `email` and `url` optional) and `plugins[]`. The optional fields are `$schema`, `description` (validate warns when it is missing), `version`, `metadata.description`, `metadata.version`, `metadata.pluginRoot` (v2.1.239+), `forceRemoveDeletedPlugins`, `allowCrossMarketplaceDependenciesOn` and `renames` (v2.1.193+). Each plugin entry is validated on its own, so one bad entry does not fail the whole marketplace. Unknown keys are ignored at load time and reported as warnings by `validate`. | source: [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Marketplace name rule: letters, digits, `.`, `_` and `-`; must start with a letter or digit; no `..`. The name comes from the file's `name` field, not from the repo name. A user can register only one marketplace per name. | source: [MR], [HM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: The following names are reserved:
  - The official names (`claude-plugins-official`, `claude-code-plugins`, `anthropic-marketplace`, ... 14 in all), the community names (`claude-community`, `claude-plugins-community`, `healthcare`) and the directory names. These are allowed only when the source is a github/git repo under `github.com/anthropics/`.
  - Impersonating names (for example `official-claude-plugins`), any non-ASCII name, and names with control or bidi characters.
  - Alternate spellings of reserved names (v2.1.280+).
  - `inline`, `builtin`, `skills-dir`, `synced` and `claude-plugin-test`.
  - `npm`, `pip`, `uv`, `cargo`, `github` and `gh` in any casing (v2.1.275+).
  - The `claudeai-` prefix.

  A marketplace already registered under a now-refused name stops loading. | source: [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Plugin entry fields:
  - `name` and `source` are required. `name` uses the same character rule as the marketplace name, and is the install id even when `plugin.json` sets a different name.
  - Optional: `description`, `version`, `category` and `tags` (free-form), `strict` (default true), `relevance`, `dependencies`, `defaultEnabled` (default true), `displayName`, `metadata` (free-form; Claude Code does not read it), and `headers`/`headersHelper` (archive auth, v2.1.238+).
  - An entry also accepts every `plugin.json` field.
  - Before install, the client can read `plugin.json` only for relative-path entries. For remote sources, users see only the entry's own fields until they install. | source: [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Strict mode:
  - When the fetched plugin has no `plugin.json`, the entry is the manifest.
  - With `strict: true` (the default) and a `plugin.json` present, the entry's component fields are appended to `plugin.json`.
  - With `strict: false` and both declaring components, the plugin fails to load with "conflicting manifests". | source: [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Minimal example from the docs: `{"name":"my-marketplace","description":"Plugins for my team","owner":{"name":"Your Name"},"plugins":[{"name":"my-first-plugin","source":"./plugins/my-first-plugin","description":"..."}]}` | source: [CM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class pattern
- claim: The real official catalog sets `$schema: https://anthropic.com/claude-code/marketplace.schema.json` and `owner {name, email}`, and uses a `renames` map with 9 entries. It lists 315 plugins: 53 relative-path, 98 `git-subdir` and 164 `url` (git) sources. All 262 external entries pin both `ref` (a tag such as `v1.5.5`) and a 40-character `sha`. Only 14 entries carry a `version` field. | source: [OFF] | Anthropic | live file | accessed 2026-10-04 | confidence high | class pattern

### 2. Plugin manifest (`plugin.json`)
- claim: The manifest is optional. Without it, components load from the standard layout and the name comes from the marketplace entry or the directory name. When present it lives at `<plugin>/.claude-plugin/plugin.json`. `name` is the only required key; it allows no spaces, `@`, `:`, path separators, control characters or bidi characters, and kebab-case is recommended. Every component is namespaced `<plugin-name>:<component>`. | source: [MF] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Fields:
  - Metadata: `displayName`, `version`, `description`, `author{name req, email, url}`, `homepage` (must parse as a URL, or the plugin fails to load), `repository`, `license` (SPDX), `keywords`, `metadata`.
  - Directory-listing fields, which Claude Code ignores: `icon`, `documentationUrl`, `supportUrl`, `privacyPolicyUrl`, `termsOfServiceUrl`.
  - `defaultEnabled`, `dependencies`, `settings`, `userConfig`, `channels`, `types`.
  - Component keys: `skills`, `commands`, `agents`, `hooks`, `mcpServers`, `lspServers`, `outputStyles`, `workflows`, and `experimental{themes,monitors,evals}`.
  - Unknown top-level keys are stripped with a validate warning. Unknown keys inside the strict sub-objects (userConfig options, channels, lspServers, monitors) are errors and stop the plugin loading. | source: [MF] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: `version` is "A version string, not checked against semver". Setting it pins users to that version until the author changes it. | source: [MF] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Standard layout: `skills/<name>/SKILL.md`, `commands/`, `agents/`, `hooks/hooks.json`, `.mcp.json`, `.lsp.json`, `output-styles/`, `workflows/`, `themes/`, `monitors/monitors.json`, `bin/` (added to the Bash tool's PATH), and `settings.json`. Component paths must stay inside the plugin root; `..`, outward symlinks and backslash paths on POSIX are rejected with "path escapes plugin directory". | source: [MF], [LD] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: `claude plugin validate` errors on plugin names that pass for Anthropic's own: the prefixes `claude-`, `anthropic-` and `cc-plugin-`, and `official` next to `claude`. It only warns on other uses of `claude` as a whole word. Only `init`, `tag` and `validate` check this; the loader still installs such plugins. | source: [MF] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature

### 3. Plugin source types and fetching
- claim: Plugin source types, each written as `"source": {"source": "<type>", ...}` (or a bare string for a relative path):
  - Relative path (`./...`, or a bare name under `metadata.pluginRoot`).
  - `github {repo, ref, sha}`.
  - `url {url, ref, sha}`: any git URL.
  - `git-subdir {url, path, ref, sha}`: sparse checkout or partial clone.
  - `npm {package, version, registry}`: install scripts never run.
  - `archive {url, sha256}`: a zip over HTTPS, v2.1.224+.
  - `command {command, timeout, mode}`: runs on the user's machine after the user accepts the exact command string, v2.1.229+.

  There is no `pip` plugin source; `pip` appears only as a reserved marketplace name. | source: [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Pinning: `ref` is a branch or tag (default branch if omitted). `sha` is a full 40-character lowercase commit; when both are set, `sha` wins, so the install survives a deleted tag if the commit is still reachable. Some servers, such as CodeCommit, cannot fetch by SHA. | source: [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: The `archive` source is the closest analogue to a "single zip over HTTPS" device store. Its rules:
  - The URL must be `https://` and must not point at a loopback, link-local or cloud-metadata host.
  - The plugin root may sit at the top of the zip or one directory down.
  - Optional `sha256` (64 hex characters): a mismatch refuses the install.
  - Download limits: 256 MiB, 120 s, at most 5 redirects, all https.
  - Extraction limits: 100,000 entries, 512 MiB per file, 1 GiB total, compression ratio no more than 50x (zip-bomb guard).
  - A cross-origin redirect drops the configured headers.

  Quote from the changelog: "Added `archive` plugin source: install plugins from a zip over HTTPS without git or npm, with optional SHA-256 pinning" (v2.1.224, 2026-08-07). | source: [MR], [HM], [CL], [NPM] | Anthropic | live doc; CL v2.1.224 = 2026-08-07 | accessed 2026-10-04 | confidence high | class feature
- claim: Marketplace sources (where the catalog itself comes from) are a separate namespace from plugin sources:
  - `url`: a direct link to `marketplace.json`, with `headers`/`headersHelper`.
  - `github {repo, ref, path, sparsePaths}`, `git {url, ref, path, sparsePaths}`.
  - `file {path}`, `directory {path}`.
  - `settings {name, plugins, owner}`: an inline catalog with no hosted file.
  - `npm` exists but "not yet implemented".

  A `url` catalog fetches ONLY the JSON file (at most 5 MiB, 10 s response), so relative-path entries cannot resolve and every entry needs a self-fetchable source (github, archive, ...). | source: [MR], [HM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Fetching works as follows. Git sources are cloned (sparse or partial for `git-subdir`). npm uses the user's npm client. Archive and url sources are fetched over HTTPS. Every marketplace plugin except local in-place ones is copied into `cache/<marketplace>/<plugin>/<version>/`, and the plugin loads from that copy. | source: [LD], [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature

### 4. Adding, listing, removing marketplaces; on-disk state
- claim: `/plugin marketplace add` (or `claude plugin marketplace add`) accepts:
  - `owner/repo`, optionally pinned as `owner/repo#ref` or `@ref`.
  - A full git URL, with `#ref`.
  - A local directory or path to a `.json` file (start with `./`, or it is read as GitHub shorthand).
  - An `https://` URL to a hosted `marketplace.json`; users then need no git for the catalog.

  `--sparse` sets `sparsePaths`, and `--scope project` writes the entry to `.claude/settings.json`. `marketplace list`, `update <name>` and `remove <name>` exist; remove uninstalls every plugin from that marketplace. `/plugin install <p> --marketplace <src>` adds and installs in one step after a confirmation (v2.1.275+). | source: [IN], [HM], [CM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: The plugins root is `~/.claude/plugins`, overridable with `CLAUDE_CODE_PLUGIN_CACHE_DIR`. It holds:
  - `cache/<marketplace>/<plugin>/<version>/`
  - `data/<plugin-id>/`: persistent across updates, exposed as `${CLAUDE_PLUGIN_DATA}`
  - `marketplaces/<name>/`: the clone or download; local sources have no copy
  - `installed_plugins.json` and `known_marketplaces.json`: the latter records `installLocation` and `autoUpdate`
  - `flagged-plugins.json`: plugins delisted by their marketplace
  - `synced/` and `.trash/`

  Old version directories get an `.orphaned_at` marker and are swept 14 days later, so running sessions keep working. | source: [LD] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature

### 5. Namespacing and identity
- claim: The install id is `<entry-name>@<marketplace-name>`. It is the key written under `enabledPlugins` in settings. The manifest name is the component prefix (`plugin:skill`). Keep the entry and manifest names equal; installing by a differing manifest name gives "not found". | source: [CM], [LD] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Name conflicts across origins compare manifest names. Precedence, highest first:
  1. Managed `enabledPlugins`.
  2. `--plugin-dir` / `--plugin-url` (silently shadows a marketplace plugin).
  3. An installed marketplace plugin.
  4. A skills-directory plugin.
  5. A plugin synced from claude.ai.

  Two marketplaces cannot share a name. Two marketplaces can list the same plugin name, because the `@marketplace` suffix disambiguates the ids, but for the same manifest name the docs give an order only across origins. | source: [LD], [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence med (same-origin cross-marketplace tie-break not stated) | class feature
- claim: The `renames` map (former name to new name, or `null` when removed) migrates users' settings keys automatically. It is treated as append-only and validate checks that chains resolve (v2.1.193, 2026-06-25). `forceRemoveDeletedPlugins: true` uninstalls delisted plugins at session start and shows them under "Flagged". | source: [HM], [CL], [NPM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature

### 6. Updates
- claim: Version detection. The client computes a version and updates only when it differs from what `installed_plugins.json` records. Resolution order:
  1. `plugin.json` `version`.
  2. The entry's `version`.
  3. A source-derived value: a 12-character commit SHA for github/url/git-subdir (git-subdir also hashes the path); a 12-character prefix of the `sha256` pin or of the downloaded file's digest for archive; `unknown` for npm and for non-git local sources.

  A pinned `"version": "1.0.0"` therefore freezes users however many commits land; omitting it tracks commits. | source: [LD], [HM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Manual updates: `claude plugin update <plugin>@<mkt>`, "Update now" in `/plugin`, or "Update marketplace" (refreshes the listing and updates its plugins). There is no update-all command. `claude plugin marketplace update` with no name refreshes listings only and does not update plugins. Installing `name@marketplace` refreshes that catalog first, unless it was refreshed in the last 30 s or is a local or settings source. | source: [IN], [LD] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Auto-update is per marketplace and `marketplace.json` has no field to turn it on. Defaults: ON for official names (except `knowledge-work-plugins` and `first-party-plugins`) and for claude.ai marketplaces; OFF for every third-party, community and local marketplace. A user toggle under `/plugin` > Marketplaces writes `autoUpdate` to `known_marketplaces.json`, and admins set `autoUpdate` on `extraKnownMarketplaces`. The pass runs after a random delay of up to 10 minutes following the first message. The running session keeps its loaded version and shows "Run /reload-plugins to apply". The per-marketplace toggle arrived in v2.0.70 (2025-12-15). | source: [LD], [HM], [IN], [CL], [NPM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Release channels are not a built-in concept. The documented pattern is two differently named marketplaces whose entries point at different refs (`stable` and `latest`). One marketplace serves exactly one version of each plugin at a time. | source: [HM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class pattern

### 7. Trust and security model
- claim: Installing shows the same generic trust warning for every marketplace: "Make sure you trust a plugin before installing, updating, or using it. Anthropic does not control what MCP servers, files, or other software are included in plugins and cannot verify that they will work as intended or that they won't change." Admins can append text with `pluginTrustMessage`. In a session, `/plugin install` opens a details pane listing the components to be installed ("Will install") before the user picks a scope. | source: [SE], [IN] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Integrity mechanisms are limited to the following, with no signing:
  - The archive `sha256` pin, carried in the catalog.
  - Git commit `sha` pins.
  - Name-tier enforcement: official and community names are accepted only from `github.com/anthropics/` sources; otherwise "Marketplace is registered from an untrusted source".
  - For `claude-community` entries pinned to a SHA, Claude Code "refuses to install a different commit".

  The docs describe no catalog or package signature. | source: [SE], [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Managed controls:
  - `strictKnownMarketplaces` (alias `allowedMarketplaces`): an allowlist of source objects. `[]` blocks everything. It supports `github` `owner/*`, `hostPattern` and `pathPattern` regexes, with exact matching on repo/url + ref + path.
  - `blockedMarketplaces`: checked before the allowlist.
  - `extraKnownMarketplaces` (alias `additionalMarketplaces`).
  - `enabledPlugins`: true force-enables, false blocks and hides.
  - `disableSideloadFlags`, `disableCommandPluginSources`, `allowManagedHooksOnly`, `strictPluginOnlyCustomization`, `pluginTrustMessage`, `syncClaudeAiPlugins`.

  The lists apply before any download (add, install, update, refresh, auto-update) and again at session start. Enforcement on install, update and autoupdate arrived in v2.1.117 (2026-04-21). | source: [OR], [CL], [NPM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Review: "Anthropic doesn't review third-party marketplaces." The community marketplace holds third-party plugins "that their authors submitted to Anthropic". The official marketplace does not take portal submissions; listings go through partner contacts. Anthropic's claude.ai directory takes submissions from a developer portal, requires a paid plan, and reviews each version before publishing; the review details are on claude.com and were not fetched. The web catalog marks some plugins "Anthropic verified". | source: [AM], [PB] | Anthropic | live doc | accessed 2026-10-04 | confidence med (review process itself not read) | class pattern
- claim: Threat surface named by Anthropic: hooks and stdio MCP servers run with full user privileges outside the sandbox, and auto-update means "the files you reviewed can change on disk". | source: [SE] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class pattern

### 8. Validation tooling
- claim: `claude plugin validate <path> [--strict] [--json]` (`--strict` v2.1.145+, `--json` v2.1.259 = 2026-09-02) validates plugins, marketplaces or skill directories and returns CI-usable exit codes.
  - Marketplace checks: JSON syntax, required fields, naming rules, relative sources containing `..`, unknown keys (warnings), each relative plugin's `plugin.json`, entry/manifest version mismatches, and `renames` chain resolution.
  - Plugin checks: type mismatches, missing or escaping paths, frontmatter, `hooks.json`, and MCP entries (v2.1.281).
  - Validate reads only local files. A missing source directory, a wrong remote repo or path, and reserved names surface only at add or install time.

  It shipped as `/plugin validate` at the launch in v2.0.12 (2025-10-09). | source: [CM], [MF], [MR], [CL], [NPM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature

### 9. Team distribution
- claim: A repo's committed `.claude/settings.json` can carry `extraKnownMarketplaces` and `enabledPlugins`. The entries apply only after the contributor accepts the workspace trust dialog and are ignored silently in untrusted folders. Relative-path plugins then load; externally sourced plugins show "enabled in project settings but isn't installed" until each contributor runs `claude plugin install <p>@<m> --scope project`. Cloud sessions never register repo marketplaces, because they never show the trust dialog. Repo-level `extraKnownMarketplaces` has existed since the v2.0.12 launch (2025-10-09). | source: [OR], [LD], [CL], [NPM] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class feature
- claim: Settings example from the docs: `{"extraKnownMarketplaces":{"your-marketplace":{"source":{"source":"git","url":"https://git.example.com/your-org/your-marketplace.git","ref":"main"}}}}`. Allowlist example: `{"strictKnownMarketplaces":[{"source":"github","repo":"your-org/*"},{"source":"hostPattern","hostPattern":"^git\\.example\\.com$"}]}` | source: [MR] | Anthropic | live doc | accessed 2026-10-04 | confidence high | class pattern

### Timeline (CHANGELOG version mapped to npm publish date)
- v2.0.12 (2025-10-09): plugin system released, with marketplaces, `extraKnownMarketplaces` and `/plugin validate`.
- v2.0.70 (2025-12-15): per-marketplace auto-update toggle.
- v2.1.117 (2026-04-21): allowlist and blocklist enforced on install, update and autoupdate.
- v2.1.193 (2026-06-25): `renames` followed automatically.
- v2.1.224 (2026-08-07): `archive` zip source with sha256.
- v2.1.238 (2026-08-20): `headersHelper`.
- v2.1.259 (2026-09-02): `validate --json`.
- v2.1.280 (2026-09-22): auto-update honours git credential helpers.
- v2.1.281 (2026-09-23): validate gains MCP checks.
- v2.1.289 (2026-10-03): current release.

The system is still changing fast; many rules above carry version gates from the last 2 months. | source: [CL], [NPM] | Anthropic / npm | accessed 2026-10-04 | confidence high | class version

## Leads worth chasing
- Device-relevant pattern: a `url` marketplace source (a single HTTPS JSON, at most 5 MiB) combined with `archive` entries (HTTPS zip with `sha256` in the catalog) needs no git client on the device. It is a direct analogue of the target store; check whether its integrity chain (HTTPS catalog, then sha256 per package) is enough without signing.
- Version semantics worth copying or adapting: an explicit `version` takes precedence, otherwise the content hash is the version. The cache path includes the version, old versions are swept after a grace period, and a persistent `data/` directory survives updates. For a small device, consider a "keep one previous version" rule instead of 14 days.
- Catalog-driven lifecycle: `renames` (append-only), `forceRemoveDeletedPlugins` and the "Flagged" list for delisted items. These map well onto a game store that retires games.
- The zip-bomb and extraction limits (entry count, per-file size, total size, 50x ratio) are a ready-made checklist for the device's unzip path; scale them down.
- claude.com "Prepare for review" and the pre-submission checklist (https://claude.com/docs/directory/publish#prepare-for-review, https://claude.com/docs/plugins/pre-submission-checklist) cover how a curated directory reviews versions. They are relevant if the device store has a curated official catalog.
- JSON Schema at https://anthropic.com/claude-code/marketplace.schema.json (referenced by the official catalog's `$schema`). Not fetched; it could give a machine-readable schema to mirror.
- `claude plugin tag` and `<plugin>--v<version>` tags for dependency version resolution (plugins/dependencies page), not read.

## Looked for, not found
- No cryptographic signing of catalogs or plugins (no publisher keys or signature files) is described in the security, marketplace or loading docs. Integrity rests on HTTPS, git SHAs and the optional archive `sha256` only.
- No `pip` plugin source exists. `pip` is only a reserved marketplace name.
- No release-channel concept is built in (explicitly stated). There is no "update all plugins" command (explicitly stated).
- No field in `marketplace.json` lets a publisher turn on auto-update (explicitly stated).
- The doc pages show no last-updated date.
- No same-manifest-name tie-break rule between two marketplace-installed plugins from different marketplaces was found.
