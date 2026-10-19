# Matching rules

These rules define what the book does. `include/me/reference_book.hpp` is the simple implementation,
`include/me/engine.hpp` is the real one, and both must produce exactly the same events
(`tests/differential_test.cpp` checks this on random command streams).

## Units

- Prices are integer ticks (`int64`). For Kraken BTC/USD one tick is $0.10.
- Quantities are integer lots (`uint64`). For Kraken one lot is 1e-8 of the base asset.

## Priority

Price-time priority. A better price always trades first (highest bid, lowest ask). Within one
price, orders trade in the order they arrived. A partial fill does not change an order's place in
line.

## Trade price

A trade happens at the resting (maker) order's price. A buy at 105 that meets a sell resting at 100
trades at 100.

## Commands

### New order

| Type | Trades on arrival | Leftover |
|---|---|---|
| Limit | Against prices that cross its limit | Rests at its limit price |
| Market | At any price | Cancelled |
| IOC (immediate-or-cancel) | Against prices that cross its limit | Cancelled |
| FOK (fill-or-kill) | Only if the full size can trade within its limit; otherwise nothing trades | Cancelled (the full size) |
| Post-only | Never; rejected if it would trade | Rests at its limit price |

The price is ignored for market orders.

### Cancel

Removes a resting order and reports how much quantity was removed.

### Modify

Changes a resting order's price and open quantity. The quantity is the new open quantity, not a
change relative to the old one.

- Same price and the same or smaller quantity: the order keeps its place in line.
- New price or bigger quantity: the order loses its place. It is taken out of the book and sent back
  in as a limit order with the same id, so it can trade immediately if the new price crosses.
- A post-only order stays post-only. If its new price would trade, the modify is rejected and the
  order is left unchanged.

## Events

Every command returns a list of events, in this order:

1. `Rejected` alone, if the command is invalid (see below). Nothing else happens.
2. `Ack` for an accepted new order, or `Modified` for an accepted modify (with the new price and
   quantity).
3. One `Trade` per fill, in the order the fills happened.
4. `Cancelled` for quantity that will never trade: the result of a cancel, or the leftover of a
   market, IOC, or FOK order.

## Rejects

A new order is checked in this order, and the first failing check decides the reason:

1. `DuplicateId`: an order with this id is resting in the book.
2. `ZeroQty`: quantity is 0.
3. `NonPositivePrice`: price is 0 or negative (all types except market).
4. `WouldTrade`: a post-only order would trade on arrival.

A modify is checked in this order: `UnknownId`, `ZeroQty`, `NonPositivePrice`, then `WouldTrade`
for a post-only order. A rejected modify leaves the order unchanged.

A cancel or modify is rejected with `UnknownId` if no resting order has that id. That includes
orders that were fully filled or already cancelled.

Ids only have to be unique among resting orders. Once an order is filled or cancelled, its id can
be used again.

## Not handled

- Self-trade prevention. Orders have no owner, so one participant's buy can trade with their own sell.
