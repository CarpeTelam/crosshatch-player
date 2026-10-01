#!/bin/sh
# SessionStart hook (.claude/settings.json): in a cloud session only, set the container up with scripts/dev_setup.py
# and start its detached warm builds. It runs synchronously, so the cached container keeps what it installs. It always
# exits 0: a failed setup is reported in the session's context and never blocks the session.
if [ "${CLAUDE_CODE_REMOTE:-}" = true ]; then
  python3 "$CLAUDE_PROJECT_DIR/scripts/dev_setup.py" --warm ||
    echo "scripts/dev_setup.py failed ($?); its errors are above. Rerun it with: python3 scripts/dev_setup.py" >&2
fi
exit 0
