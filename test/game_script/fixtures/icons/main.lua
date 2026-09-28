-- Icons fixture: every library icon through ch.gfx.icon at each size (small
-- 32 px, medium 64 px, large 128 px) in both weights (regular and fill), a page
-- of up to three icons of one category at a time, each shown regular then fill.
-- The black pages draw black icons on white, then the white pages white icons on
-- black; each tap turns to the next page, and after the last white page back to
-- the first black one. The small and medium rows hold six cells, each name's
-- regular then its fill; the medium row sits on a light band, which shows
-- through around each icon's ink, since an icon draws only its ink pixels. The
-- large area holds the regular icons above the fill ones, each name under its
-- fill icon. Copy this folder to /.games/icons/ on the SD card
-- (fs_/.games/icons/ in the simulator).
local game = {}

-- The library by category, in docs/crosshatch/game-icons.md's order.
local NAMES = {
  { "Marks", { "x", "circle", "dot-outline", "fire", "waves" } },
  { "Card suits", { "club", "diamond", "heart", "spade" } },
  { "Dice faces", { "dice-one", "dice-two", "dice-three", "dice-four", "dice-five", "dice-six" } },
  { "Game pieces", { "boat" } },
  { "Player markers", { "square", "triangle", "star", "hexagon", "user", "users" } },
  { "Controls", {
    "arrow-left", "arrow-right", "arrow-up", "arrow-down", "arrow-u-up-left", "arrow-u-up-right",
    "arrows-clockwise", "arrow-clockwise", "shuffle", "play", "pause", "check", "plus", "minus", "info",
    "question", "lightbulb", "pencil-simple", "eraser", "eye", "eye-closed", "house", "gear-six", "sign-out",
    "trash", "timer", "hourglass", "game-controller",
  } },
  { "Status", { "warning", "flag-checkered", "trophy", "smiley", "smiley-sad" } },
}

local PER_PAGE = 3
local WEIGHTS = { "regular", "fill" }

-- Each category split into pages of up to PER_PAGE icons, in order:
-- { category = ..., part = ..., parts = ..., names = { ... } }.
local PAGES = {}
for _, entry in ipairs(NAMES) do
  local category, names = entry[1], entry[2]
  local parts = (#names + PER_PAGE - 1) // PER_PAGE
  for part = 1, parts do
    local page = {}
    for i = (part - 1) * PER_PAGE + 1, math.min(part * PER_PAGE, #names) do
      page[#page + 1] = names[i]
    end
    PAGES[#PAGES + 1] = { category = category, part = part, parts = parts, names = page }
  end
end

function game.setup(ctx)
  return {}
end

function game.status(state)
  return { turn = 1 }
end

function game.apply(state, seat, move)
  return state
end

function game.draw(state, seat, ui)
  local W = ch.screen.w
  -- ui.page runs 0 .. 2 * #PAGES - 1: the black pages, then the white ones.
  local index = ui.page or 0
  local white = index >= #PAGES
  local number = index % #PAGES + 1
  local page = PAGES[number]
  local ink = white and "white" or "black"
  ch.gfx.clear(white and "black" or "white")
  ch.gfx.text(12, 10, (white and "Icons: white " or "Icons: black ") .. number .. "/" .. #PAGES, "medium", ink)
  ch.gfx.text(W - 12, 14, "tap: next page", "small", ink, "right")
  local heading = page.category
  if page.parts > 1 then heading = heading .. " (" .. page.part .. " of " .. page.parts .. ")" end
  ch.gfx.text(12, 44, heading .. ": regular, fill", "small", ink)

  -- Small and medium: one row each, six cells across, each name's regular then
  -- its fill.
  local cell = W // (2 * PER_PAGE)
  for i, name in ipairs(page.names) do
    for w, weight in ipairs(WEIGHTS) do
      local c = 2 * (i - 1) + (w - 1)
      ch.gfx.icon(name, c * cell + (cell - 32) // 2, 72, "small", ink, weight)
    end
  end
  ch.gfx.rect(0, 116, W, 80, "light", true)
  for i, name in ipairs(page.names) do
    for w, weight in ipairs(WEIGHTS) do
      local c = 2 * (i - 1) + (w - 1)
      ch.gfx.icon(name, c * cell + (cell - 64) // 2, 124, "medium", ink, weight)
    end
  end

  -- Large: three across, regular above and fill below, each name under its fill
  -- icon.
  local third = W // 3
  for i, name in ipairs(page.names) do
    local x = (i - 1) * third + (third - 128) // 2
    for w, weight in ipairs(WEIGHTS) do
      local y = 216 + (w - 1) * 176
      ch.gfx.icon(name, x, y, "large", ink, weight)
      if weight == "fill" then
        ch.gfx.text((i - 1) * third + third // 2, y + 134, name, "small", ink, "center")
      end
    end
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then ui.page = ((ui.page or 0) + 1) % (2 * #PAGES) end
  return nil
end

return game
