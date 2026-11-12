# Commerce (the peaceful economy of a city game)

Rules block `"commerce"` plus the map list `"premises"`. A player rents or buys premises, opens a business
inside, borrows from the bank against what he owns. Code: `modules/rts/RtsCommerce.{h,cpp}`, Lua table
`commerce` (`RtsCommerceApi.cpp`), tests `aiverse_tests commerce`.

## How it works

- **Money** is an ordinary resource of the rules (`currency`), so bounties, pickups and the top bar already use it.
- **The clock** is the simulation tick. A day is `dayMinutes` of real time, a week is `weekDays` days.
- **Premises** come from the map: a door position, a kind (`shop`, `office`, `garage`, anything), a price, a
  weekly rent and `traffic` (how busy the spot is; 1 is average).
- **A business** is a type from the rules that fits some kinds of premises. Opening costs `setup`. At the end of
  every day it books `incomeDay * traffic - costDay` to whoever runs the premises (the tenant, else the owner).
- **Rent** is paid for the first week when the lease starts and then at the end of every week. A tenant who
  cannot pay is evicted and the business is closed.
- **The bank** lends up to `unsecured + collateralShare * (prices of owned premises)`. At the end of a week it
  adds `rateWeek` interest and takes the interest plus `1 / termWeeks` of the debt. A payment that is not made
  in full counts as missed; after more than `graceWeeks` missed in a row the bank takes owned premises (in map
  order, each for `collateralShare` of its price) until the debt is covered. Selling a property that the debt
  leans on is refused.
- Charges the player cannot cover (a losing business) take what he has; the rest is added to the debt.

## Deals

Everything goes through one command, `kCmdCommerce`, with `param = commercePack(op, premises, value)`, so the UI,
scripts, bots and the network use the same checks.

| Deal | Needs | Does |
|---|---|---|
| `rent` | city-owned, free, one week of rent in cash | lease starts |
| `leave` | being the tenant | lease ends, the business closes |
| `buy` | city-owned, free or rented by the buyer, the price in cash | ownership; a tenant keeps his business |
| `sell` | being the owner, debt within the limit that is left | 80 % (`sellShare`) of the price, the business closes |
| `open` + business | running the premises, no business yet, a fitting kind, `setup` in cash | business opens |
| `close` | running the premises | business closes |
| `borrow` + amount | debt + amount within the credit limit | cash and debt grow |
| `repay` + amount | a debt, some cash | both shrink |

With rules `topDown` the hero has to stand within `range` cells of the door (0: from anywhere).

## rules.json

```json
"commerce": {
  "currency": "cash", "dayMinutes": 24, "weekDays": 7, "range": 3, "sellShare": 0.8,
  "bank": {"rateWeek": 0.02, "unsecured": 2000, "collateralShare": 0.6, "termWeeks": 20, "graceWeeks": 2},
  "businesses": {
    "kiosk": {"name": "Kiosk", "kinds": ["shop"], "setup": 1500, "incomeDay": 260, "costDay": 90}
  }
}
```

## Map

```json
"premises": [
  {"id": "shop_12", "kind": "shop", "at": [40.5, 61.5], "price": 20000, "rentWeek": 700, "traffic": 1.2}
]
```

Optional: `"owner": 1` (a player owns it from the start), `"business": "kiosk"` (already running).

## Lua

Queries (simulation and view scripts): `commerce.active()`, `commerce.clock()` -> `{day, week, weekday, time}`,
`commerce.count()`, `commerce.premise(index | id)`, `commerce.account(player)` -> `{cash, debt, limit, missed,
day_net, week_net, last_week_net, last_rent, last_loan_payment}`, `commerce.businesses()`,
`commerce.can(player, deal, ...)` -> `true` or `false, reason`.

Deals: simulation scripts call `commerce.act(player, deal, ...)`; the UI sends
`game.command(player, {type = "commerce", param = commerce.param(deal, ...)})`. Arguments after the deal name:
premises (index from 1 or id), then the business id for `open`; just the amount for `borrow` / `repay`.

## Not done yet

Income is a fixed figure per day scaled by `traffic`: real customers (residents who walk in and pay) and the
statistical demand of far districts are the next step. Staff and wages, managers, renting from another player,
supplies, taxes, car dealers and garages, events for sounds and messages, any UI.
