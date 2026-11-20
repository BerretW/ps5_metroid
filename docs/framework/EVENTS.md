# Events

Events are typed, trivially copyable values. Listeners implement `IEventListener<Event>` and receive callbacks in subscription order.

```cpp
struct ItemCollected { echo::framework::EntityId actor; std::uint32_t item; };

echo::framework::EventBus<> bus;
echo::framework::Subscription token;
bus.subscribe(listener, token);
echo::framework::ScopedSubscription lifetime(bus, token);
bus.publish(ItemCollected{player, 7});
```

The caller must keep listeners alive until the subscription is removed. Prefer `ScopedSubscription`, declared after the listener so it unsubscribes first during scope exit; the raw token API remains available for explicitly managed lifetimes.

Publishing during callback dispatch appends to a bounded FIFO queue rather than recursing. The current event is delivered to listeners that were subscribed when its dispatch began; newly subscribed listeners can receive later queued events. A listener removed during dispatch is skipped immediately. Queue overflow returns `CapacityExceeded`; it is never silently dropped. Event delivery is synchronous and single-threaded; no background thread or cross-thread synchronization is provided.
