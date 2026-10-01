#!/bin/sh
# SessionStart hook (.claude/settings.json): in a cloud session only, set the container up with scripts/dev_setup.py
# and start its detached warm builds. It runs synchronously, so the cached container keeps what it installs. Only the
# hook's stdout reaches the session's context, so the script's errors go there too. It always exits 0: a failed setup
# is reported and never blocks the session.
if [ "${CLAUDE_CODE_REMOTE:-}" = true ]; then
  python3 "$CLAUDE_PROJECT_DIR/scripts/dev_setup.py" --warm 2>&1 ||
    echo "scripts/dev_setup.py failed ($?); its errors are above. Rerun it with: python3 scripts/dev_setup.py"
fi
exit 0
