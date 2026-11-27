# Draft entry for the Bambuddy wiki

For <https://wiki.bambuddy.cool/community/>, under **Hardware**. The page that goes with the
card is `docs/wiki/bambuddy-panel.md`. Written to match the entries already there. Proposed as
[maziggy/bambuddy-wiki#185](https://github.com/maziggy/bambuddy-wiki/pull/185).

---

### Bambuddy Panel

ESP32 touch panel that sits next to a printer and shows that printer's status and queue, on the inexpensive ESP32-2432S028R ("Cheap Yellow Display"). At the printer you can pause a print, confirm the plate is clear, record a print outcome, mark maintenance done or assign an inventory spool; anything that starts or stops a print needs a 1.5-second hold. Talks only to Bambuddy, through its REST API with an API key.

**Author:** loidolt · [Repository](https://github.com/loidolt/bambuddy-panel)

---

## The pull request

Against [maziggy/bambuddy-wiki](https://github.com/maziggy/bambuddy-wiki), five changes:

1. `docs/wiki/bambuddy-panel.md` from here goes to `docs/community/bambuddy-panel.md`. Its
   relative links (`../features/api-keys.md`, `../reference/api.md`) are written for that place.
2. The card above goes into `docs/community/index.md`, in the Hardware section, in the wiki's
   card markup, with the heading linking to the page:

   ```markdown
   <div class="feature-card" markdown>
   ### [:material-tablet: Bambuddy Panel](bambuddy-panel.md)
   ...the paragraph...

   **Author:** [loidolt](https://github.com/loidolt) &middot; [Repository](https://github.com/loidolt/bambuddy-panel)
   </div>
   ```
3. A line in `mkdocs.yml` under `Community Add-ons`, after Bambutton:
   `- Bambuddy Panel: community/bambuddy-panel.md`
4. `docs/demo.gif` from here goes to `docs/assets/bambuddy-panel-demo.gif`, where the wiki
   keeps its images; the page shows it from there as `../assets/...`, the way every other page
   of the wiki does (its CONTRIBUTING.md says `/assets/...`, but no page uses that).
5. A line in `docs/llms.txt`, next to Bambutton's, pointing at the new page.
