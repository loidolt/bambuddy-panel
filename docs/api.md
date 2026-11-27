# How it talks to Bambuddy

The panel calls the Bambuddy server’s HTTP REST API with the `X-API-Key` header. Its `User-Agent` is
`bambuddy-panel/<firmware-version>` (for example, `bambuddy-panel/dev`). Routes and payloads were
checked against the Bambuddy 1.3.1 and 1.3.2 source trees. Available data depends on the printer,
server version and [permissions](get-started.md#create-an-api-key).

## Reads

All paths below start with `/api/v1`. In normal operation the worker waits about four seconds
**after each polling round completes**. If the status read fails, the round ends there. A command
wakes the worker early.

| When | GET path | Purpose |
|---|---|---|
| During setup | `/printers/` | Suggest up to 16 visible printers |
| On save and each poll | `/printers/{id}/status` | Check the selected printer and read its state |
| Each successful status poll | `/queue/?status=pending,skipped,printing&printer_id={id}` | Read waiting, held and starting jobs |
| Outside preparing, slicing, printing and paused states | `/archives/last-per-printer` | Find the last print and any pending outcome question |
| At startup and about every 60 seconds | `/maintenance/printers/{id}` | Read maintenance tasks |
| At startup and about every 60 seconds | `/settings/` | Read `require_plate_clear` |
| When the printer reports slots, about every 60 seconds | `/inventory/assignments?printer_id={id}` | Match slots to inventory spools |
| When the spool picker opens | `/inventory/spools` | Read the inventory and retain up to 150 spools |

The firmware filters JSON to the fields it uses. Reads have a 512 KiB body limit and a 40 KiB
allocation budget for the filtered document. Controls that depend on an unavailable or incomplete
read wait for fresh data.

## Writes

Before sending a write, the worker reads the target again and checks it against the selection on
screen. It refreshes maintenance, assignments or the plate-clear setting early when the action needs
them. Bambuddy handles scheduling and printer acknowledgement. The panel polls again before
accepting another write.

| Action | Method and path under `/api/v1` | JSON body, if used |
|---|---|---|
| Print control (`pause`, `resume` or `stop`) | `POST /printers/{id}/print/{action}` | — |
| Confirm plate clear | `POST /printers/{id}/clear-plate` | — |
| Clear printer errors | `POST /printers/{id}/hms/clear` | — |
| Start the next staged job | `POST /queue/{item_id}/start` | — |
| Resume held jobs | `POST /queue/printer/{id}/resume` | — |
| Record an outcome | `PATCH /archives/{archive_id}` | `user_verdict` (`good` or `reject`), `user_verdict_source` (`api`) and the print’s `run_id` |
| Mark a task done | `POST /maintenance/items/{item_id}/perform` | `{}` |
| Assign a spool | `POST /inventory/assignments` | `spool_id`, `printer_id`, `ams_id`, `tray_id` and `move: true` |
| Remove a spool assignment | `DELETE /inventory/assignments/{printer_id}/{ams_id}/{tray_id}` | — |

**Skip** on the outcome screen is local to the panel. It sends no archive update.
