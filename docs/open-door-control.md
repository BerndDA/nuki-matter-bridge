# Separate "open door" control (later)

## Background

Apple Home shows only **Lock / Unlock** for Matter door locks, with no unlatch button.
If the lock has the Matter *Unbolting* feature (`CONFIG_NUKI_ENABLE_UNBOLT`), Apple Home sends
`UnlockDoor`, which by the spec means "unlock **and pull the latch**", so every Unlock
(button, Siri, automations) would open the door. Apple never sends `UnboltDoor`.

Sources:
- https://github.com/RiDDiX/home-assistant-matter-hub/issues/514
- https://riddix.github.io/home-assistant-matter-hub/devices/lock

Plan: keep Unbolting **off** (Unlock = retract the bolt only) and add a second
control on its own endpoint for opening the door.

## Option A: second lock endpoint "Front door – Open" (preferred)

- Second Door Lock endpoint. Its `UnlockDoor` sends `Unlatch` to the Nuki. Afterwards it
  always reports `Locked` again.
- Pros: Apple asks for Face ID/passcode when a lock is controlled from the lock screen,
  Control Centre or widgets. Siri needs "unlock Front door Open", so opening by accident is unlikely.
  Excluded from "turn on everything".
- Cons: a second "lock" tile that is really a button. Its `Lock` command is a no-op
  (or it just reports Locked).

## Option B: momentary switch "Open door"

- On/Off endpoint. On → `Unlatch`, then it switches itself back off after ~1 s.
- Pros: simplest, and reads as a button in Apple Home.
- Cons: Apple treats switches as harmless. There is no authentication, it can be triggered from
  widgets, and Siri's "turn on everything" would open the door. It would need an extra
  safeguard, e.g. only acting when the main lock is already unlocked.

## Notes

- Both work in simulation mode and with the real Nuki (`BridgeLockCommand::Unlatch`).
- Adding an endpoint changes the device's structure: remove the bridge from Apple Home, run
  `matter esp factoryreset` on the console, set Wi-Fi again and re-add it.
