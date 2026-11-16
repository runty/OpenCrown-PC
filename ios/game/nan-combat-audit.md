# NaN-candidate guided combat audit

On 2026-11-16 the private NaN-result candidate `a3e07c2` ran a muted, offscreen
Mac adventure audit with a disposable copy of the Darrel save. The guide used
ordinary scripted pad/tap inputs, the production TouchLoop, the public pause
API and a private read-only cursor mailbox export. The host archive was copied
from the regenerated candidate and its runtime/offline objects replaced with
the previously reviewed guided-input overrides. The candidate's 50,464
translated routines were not deliberately modified. This host is diagnostic,
not a packaged mobile app.

The run progressed through the first goblins, opened a chest, fought spiders
and lizards, used the normal Continue prompt, and reached the visible Harpy
boss. Its health bar decreased during combat, but the guided fighter did not
defeat it. After an intentional SIGINT, the host acknowledged unload and had
no reported guest or host error. `HARPY` remains an unmet component checkpoint
because the game reports the component as `GOBLIN`; the host exited 1 and this
is not a passing adventure-clear checkpoint. Captures and the Continue prompt
are visual observations, not proof of full playthrough completion.

The 46,844 rendered frames include guide pauses and are not performance data.
Full-content texture verification checked 1,198,183 version hits, representing
4,675,989,809,152 logical bytes, with zero proof failures. Logical byte spans
are not measured physical bandwidth. The save hash remained
`728f0ecb5c92c57a833b731b9b2943bc059d2abd2383a043a280e6c8cf937bac`.
The player-skipped manual Inn save test was not performed.

A first attempt reached the waterfall corridor but its driver exited at the end
of its scripted steps, ending the host without a verified clean unload. It is
retained as incomplete and excluded. The corrected second driver stayed alive
until the explicit stop. Commands, source/object hashes, report, screenshots and
independent verification are under
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/adventure-guided/run2`.
No physical device or live save was used. Boss clear, mobile correctness,
sustained performance and the rest of the port acceptance remain open.
