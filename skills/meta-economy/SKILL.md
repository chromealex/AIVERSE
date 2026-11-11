---
name: meta-economy
description: Configure AIVERSE currencies, inventories, stores, crafting recipes, targeted offers, payments and inbox rewards using the existing C++ meta library.
---

- Read `engine/meta/Meta.h` and economy/payment sections of `docs/meta-game.md`.
- Define assets and caps, then offers with cost/reward. Use one offer algorithm for exchanges, crafting and gifts.
- Use Condition all/any/none, minimum/maximum, tags and Schedule for targeting. Configure limit/cooldown/priority/group; group is a display category.
- Give each operation a stable retry ID. Commit the complete profile plus receipts with revision/CAS. Reload on Conflict; never retry with a fresh ID.
- Expose offer ID, not arbitrary grant/spend bundles, to an online client.
- For IAP reuse `engine/services/Store.h`. On the backend use reserveOrder to atomically reserve the limit and snapshot the reward. Validate player/product/receipt against that order, fulfillOrder with provider-prefixed transaction ID, durably commit, then finish. Cancel only after authoritative terminal nonpayment. Never fulfill Pending or a client-asserted receipt.
- fulfillOrder honors the reserved reward past offer expiry/catalog changes. The low-level fulfill is only for a separate external order system; never mix it with reserveOrder for the same purchase.
- Use deliver/claimMail for deferred awards; preserve immutable mail ID and expiry. Do not create a second balance owner beside the legacy Lua profile.
- Validate changed economy JSON with aiverse_meta_validate. Exercise retry, insufficient funds, cap overflow, failed commit and restore with preserved receipts.

- Paid offers must have empty cost (schema enforced); virtual offers use cost. Keep reserved reward assets/caps compatible. If fulfillment returns Limit, retain Pending, resolve capacity server-side, then retry; do not finish an ungranted payment.
