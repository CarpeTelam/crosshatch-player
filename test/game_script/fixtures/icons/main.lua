-- Icons fixture: every library icon through ch.gfx.icon at each size (small
-- 32 px, medium 64 px, large 128 px), a page of up to six icons of one category
-- at a time. The black pages draw black icons on white, then the white pages
-- white icons on black; each tap turns to the next page, and after the last
-- white page back to the first black one. The medium row sits on a light band,
-- which shows through around each icon's ink, since an icon draws only its ink
-- pixels. Copy this folder to /.games/icons/ on the SD card (fs_/.games/icons/ in
-- the simulator).
local game = {}

-- The library by category, in docs/crosshatch/game-icons.md's order.
local NAMES = {
  { "Marks", { "mark_x", "mark_o", "mark_dot", "mark_hit", "mark_miss" } },
  { "Card suits", { "suit_club", "suit_diamond", "suit_heart", "suit_spade" } },
  { "Dice faces", { "die_1", "die_2", "die_3", "die_4", "die_5", "die_6" } },
  { "Board pieces", {
    "piece_king", "piece_queen", "piece_rook", "piece_knight", "piece_bishop", "piece_pawn", "piece_ship",
  } },
  { "Player markers", {
    "marker_circle", "marker_square", "marker_triangle", "marker_star", "marker_hexagon", "player", "players",
  } },
  { "Controls", {
    "arrow_left", "arrow_right", "arrow_up", "arrow_down", "undo", "redo", "restart", "rotate", "shuffle",
    "play", "pause", "check", "plus", "minus", "info", "help", "hint", "pencil", "eraser", "show", "hide",
    "home", "settings", "leave", "trash", "timer", "hourglass", "game_controller",
  } },
  { "Status", { "warning", "flag_checkered", "trophy", "smiley", "smiley_sad" } },
}

local PER_PAGE = 6

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
  ch.gfx.text(12, 44, heading, "small", ink)

  -- Small and medium: one row each, one icon per sixth of the width.
  local cell = W // PER_PAGE
  for i, name in ipairs(page.names) do
    ch.gfx.icon(name, (i - 1) * cell + (cell - 32) // 2, 72, "small", ink)
  end
  ch.gfx.rect(0, 116, W, 80, "light", true)
  for i, name in ipairs(page.names) do
    ch.gfx.icon(name, (i - 1) * cell + (cell - 64) // 2, 124, "medium", ink)
  end

  -- Large: three across, each icon named under it.
  local third = W // 3
  for i, name in ipairs(page.names) do
    local column, row = (i - 1) % 3, (i - 1) // 3
    local x = column * third + (third - 128) // 2
    local y = 216 + row * 176
    ch.gfx.icon(name, x, y, "large", ink)
    ch.gfx.text(column * third + third // 2, y + 134, name, "small", ink, "center")
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then ui.page = ((ui.page or 0) + 1) % (2 * #PAGES) end
  return nil
end

return game
