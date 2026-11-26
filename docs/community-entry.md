# Draft entry for the Bambuddy wiki

For <https://wiki.bambuddy.cool/community/>, under **Hardware**, once every button has been
pressed against a real Bambuddy, the repository is public and it has a release. The page that
goes with the card is `docs/wiki/bambuddy-panel.md`. Written to match the entries already there. Drop the
"Early" box once the panel has been in use for a while.

---

### Bambuddy Panel

An ESP32 touch panel displays one printer’s status and queue. At the printer you can pause a print, confirm the plate is clear, record a print outcome or assign an inventory spool. It connects to Bambuddy through its versioned HTTP API.

!!! warning "Early"

    The author flags the panel as early: expect rough edges. Track the repo for releases.

**Author:** loidolt · [Repository](https://github.com/loidolt/bambuddy-panel)

---

## The pull request

Against [maziggy/bambuddy-wiki](https://github.com/maziggy/bambuddy-wiki), four changes:

1. `docs/wiki/bambuddy-panel.md` from here goes to `docs/community/bambuddy-panel.md`. Its
   relative links (`../features/api-keys.md`, `../reference/api.md`) are written for that place.
2. The card above goes into `docs/community/index.md`, in the Hardware section, in the wiki's
   card markup, with the heading linking to the page:

   ```markdown
   <div class="feature-card" markdown>
   ### [:material-tablet: Bambuddy Panel](bambuddy-panel.md)
   ...the paragraph and the "Early" box...

   **Author:** [loidolt](https://github.com/loidolt) &middot; [Repository](https://github.com/loidolt/bambuddy-panel)
   </div>
   ```
3. A line in `mkdocs.yml` under `Community Add-ons`, after Bambutton:
   `- Bambuddy Panel: community/bambuddy-panel.md`
4. `docs/demo.gif` from here goes to `docs/assets/bambuddy-panel-demo.gif`, where the wiki
   keeps its images; the page shows it from there.
