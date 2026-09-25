# Evidence Qt-local v0.1

This branch deliberately avoids the proposed `GET /api/v1/parking-evidence`
endpoint. The Evidence page opens as one locally sorted timeline from
`ParkingViewState::slotImages`, which has already arrived through the existing
slot and session-image APIs. During one app run, it retains the last non-empty
images observed for each slot in an Evidence-only Qt cache. Slots are filters,
not the primary navigation.

## What is processed in Qt

- Groups image variants into capture groups.
- Attaches the known slot, slot state, and plate number.
- Shows all loaded capture groups by `captured_at DESC` in the UI.
- Narrows the time-ordered result with Slot, Plate, and Reason filters.
- Displays the selected image using the existing image loader.
- Keeps already loaded images visible when a subsequent parking-status poll
  omits its `images` field.
- On entering or refreshing the All slots view, requests each currently known
  slot's existing detail/current-session image flow. Selecting one slot asks
  for that slot only.
- Repeats that request every five seconds while the Evidence page is visible;
  the timer stops when the operator leaves the page. Automatic refresh keeps
  the current cards visible and only re-renders changed evidence resources.
- Resolves the First and Latest cards only within the selected timeline row's
  exact `slot_id + session_id`; rows from another slot or session are never
  combined into one pair.
- Uses the authoritative event session ID when an event image item omits it;
  without any session ID, the UI refuses to guess a First/Latest pair.
- Lets the operator collapse and expand the Capture timeline while keeping the
  current First/Latest cards and refresh state intact. Collapsing the timeline
  gives the released vertical space to both images instead of stretching an
  empty capture-card area below a fixed-height image.
- Supports Ctrl/Shift multi-selection in the Capture timeline and downloads
  one deduplicated First/Latest pair per selected `slot_id + session_id`.
  The timeline itself also renders one row per session pair, with separate
  First capture and Latest capture columns instead of one row per image.
  Image filenames include a shared export ID plus capture time, OCR, and reason;
  the selected folder also receives a matching `evidence_metadata_*.csv` with
  slot, session, capture, OCR, separately labeled session plate, source, and
  result details. Missing per-image OCR is exported as `unconfirmed`.

## Deliberate boundary

It does **not** obtain historical sessions or use a new history endpoint. The
local cache is cleared when the app restarts and is not a server-side evidence
store. This v0.1 flow makes one current-session request per currently known
slot, so it can still show no result when those sessions have no evidence.
Neither version can obtain all historical sessions without a server-side history
query.
