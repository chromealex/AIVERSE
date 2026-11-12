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

## Staff, managers, customers, counters, homes

- **Staff.** A business type may need `staff` employees (`wageWeek` each, charged at the week's end before rent
  and the bank). With fewer hired the income shrinks in proportion; with none it earns nothing.
- **Manager.** Without one (`managerWageWeek`) a business earns only `unmanagedShare` of its income on a day its
  owner did not come by the door. With one it runs at full income on its own: this is what makes a chain possible.
- **Walk-in customers.** `commerce.visit(premises)` makes one customer pay the type's `ticket` at once. What real
  customers paid during a day is taken off the day's statistical income, so near the player money arrives
  person by person and far away as one booking, with the same total; a busy day can exceed it by half.
- **Counters.** `offers` are things sold at premises of given kinds: a weapon with ammo (rules `arsenal`) or a
  vehicle (it appears at the kerb and is listed in the buyer's account). Map premises with `"public": true`
  belong to the city: a gun shop, a car dealer, a bank; they are not for rent or sale.
- **Homes.** Premises of the kinds in `storage` (default `flat`) have a stash for money and ammo. Only the one
  who lives there reaches it (`commerce.premise(id, player)` shows its contents to him alone); death does not
  touch it. A lease cannot be ended or a home sold with things in the stash; an evicted tenant gets the money back.

More deals: `hire`, `fire`, `manager` (toggle), `purchase` + offer id, `stash` / `unstash` + amount,
`stash_weapons`, `take_weapons`.

In the client (rules `topDown`): at a door the top bar names the place, **E** opens the list of deals, **1..9**
make them. The day and the debt are shown next to the money. Lua for missions: `driving.car_of(player)`,
`driving.speed(vehicle)`, `driving.driver(vehicle)`, `driving.is_vehicle(unit)`.

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

## Adding it to a city project

`tools/citygen/add_commerce.py <project>` turns the buildings already on the map into premises (a door on the
nearest pavement, a price by kind and by how central the spot is) and adds the `commerce` block with business
types to the rules. It is additive and repeatable: existing entries are kept.

## Not done yet

Residents with wallets, homes and jobs (customers are whoever walks by; staff are numbers, not people), district
demand, renting premises out to others, supplies, taxes, insurance, garages for bought cars, furniture, touch
buttons for the deals, sounds and messages for the weekly settlement.
