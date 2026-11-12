# Careers (paths with ranks and perks)

Rules block `"careers"`. Code: `modules/rts/RtsCareer.{h,cpp}`, Lua table `career` (`RtsCareerApi.cpp`), tests
`aiverse_tests career`.

A career is a ladder of ranks. A rank is reached at a number of points and brings perks: plain names that
scripts and other systems ask about (`career.has_perk(player, "hotwire")`). A player may follow several careers
at once; points are counted per career, perks add up. Leaving a career loses its points.

The engine keeps the ladders, the membership and the perks. Who earns points and for what is the game's
business: scripts call `career.add`. A career with `"open": true` is joined and left by the player's own command
(`kCmdCareer`, Lua `game.command(p, {type = "career", param = op | index << 8})`, op 1 join, 2 leave); the
others only by a script (a gang takes you in).

```json
"careers": {
  "police": {"name": "Police", "open": true, "ranks": [
    {"name": "Cadet", "points": 0},
    {"name": "Patrol officer", "points": 20, "perks": ["siren"]},
    {"name": "Detective", "points": 250, "perks": ["records"]}]}
}
```

Lua: `career.active()`, `career.list()`, `career.status(player, id)` -> `nil` or `{points, rank, rank_name,
next_points}`, `career.has_perk(player, perk)`; simulation scripts: `career.join`, `career.leave`,
`career.add(player, id, points)` -> ranks gained or lost.

Fines belong to the commerce module: `commerce.fine(player, amount)` takes the money from the pocket and turns
what is missing into bank debt.

`tools/citygen/add_careers.py <project>` adds five ladders to a city project (business, police, ambulance, fire
brigade, gang). State is hashed and saved; games without the block are untouched.

Not done yet: what the perks do (they are names so far), joining from the UI, the work that earns points on
every path except business.
