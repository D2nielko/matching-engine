# Matching rules

These rules define what the book does. `include/me/reference_book.hpp` implements them, and
`tests/reference_book_test.cpp` checks them. Any faster book must produce exactly the same events.

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

## Order types

Limit order: trades against the opposite side for as long as prices cross its limit, then rests in
the book with whatever is left.

Market order: trades against the opposite side at any price until it is filled or the opposite side
is empty. Whatever is left is cancelled and never rests. The price argument does not exist for
market orders.

Cancel: removes a resting order and reports how much quantity was removed.

## Events

Every request returns a list of events, in this order:

1. `Rejected` alone, if the request is invalid (see below). Nothing else happens.
2. `Ack` for an accepted new order.
3. One `Trade` per fill, in the order the fills happened.
4. `Cancelled` for quantity that will never trade: the result of a cancel, or the unfilled rest of
   a market order.

## Rejects

A new order is checked in this order, and the first failing check decides the reason:

1. `DuplicateId`: an order with this id is resting in the book.
2. `ZeroQty`: quantity is 0.
3. `NonPositivePrice`: limit price is 0 or negative (limit orders only).

A cancel is rejected with `UnknownId` if no resting order has that id. That includes orders that
were fully filled or already cancelled.

Ids only have to be unique among resting orders. Once an order is filled or cancelled, its id can
be used again.

## Not handled yet

- Self-trade prevention. Orders have no owner yet, so one participant's buy can trade with their own sell.
- Modify (cancel/replace), IOC, FOK, post-only.
- Sequence numbers and timestamps on events.
