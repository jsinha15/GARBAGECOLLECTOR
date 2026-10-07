# Deployment plan: HeapLens on Render (free)

**Goal:** a public HTTPS link to HeapLens, built from your GitHub repository, free of charge, redeploying automatically on
every `git push`.

**Decisions already made:** host = Render free web service · packaging = the repo's `Dockerfile` · state = one global runtime
shared by all visitors (per-visitor sessions are a later phase).

---

## 0. Status — what is already prepared in this repo

| Item | Status |
|---|---|
| Server reads Render's `$PORT` and listens on all interfaces | done, tested locally |
| `GET /healthz` returns `ok` (Render's health check) | done, tested locally |
| Stalled-client timeouts (5 s) so one bad connection can't hang the server | done |
| `Dockerfile` (GCC build stage → static binary → slim runtime, port from `$PORT`) | written; **not built in Docker yet** |
| `render.yaml` Blueprint (Docker, free plan, health check `/healthz`) | written; **not run on Render yet** |
| `.dockerignore`, `.gitignore` | done |
| Same compiler flags as the Dockerfile (`-O2 -static`) | built and all 120 tests pass on the dev machine |
| UI examples run against the real server | done (all 11) |

The only untested pieces are the Docker build and Render itself — expect at most small fixes at step 2.

---

## 1. Put the project on GitHub (≈10 min)

You need: a GitHub account, and Git installed (`git --version`; get it from git-scm.com if missing).

1. On github.com → **New repository** → name `heaplens` → *Public* (Render's free tier works with public or private repos) →
   **do not** add a README/license (the repo already has them) → *Create*.
2. In PowerShell:
   ```powershell
   cd D:\Practice\GARBAGECOLLECTOR
   git init
   git add .
   git status                       # check: no build\ folder, no .exe files listed
   git commit -m "HeapLens: garbage collector lab"
   git branch -M main
   git remote add origin https://github.com/<your-username>/heaplens.git
   git push -u origin main
   ```
   (GitHub will ask you to sign in the first time.)

## 2. Create the Render service (≈10 min, most of it waiting for the build)

1. Sign up at **render.com** with your GitHub account.
2. **New +** → **Blueprint** → connect/select the `heaplens` repository → Render reads `render.yaml` → **Apply**.
   *(Manual alternative: **New + → Web Service** → pick the repo → set* **Runtime: Docker**, **Branch: main**,
   **Instance type: Free**, **Health Check Path: `/healthz`** *→ Create.)*
3. Watch the **Logs** tab. A healthy build ends with the Docker image being pushed and the service going **Live**.
   The first build takes a few minutes (it compiles the C++ inside Docker).
4. Your URL is shown at the top of the service page: `https://heaplens-xxxx.onrender.com`.

## 3. Verify (2 min)

- [ ] `https://<your-url>/healthz` shows `ok`.
- [ ] `https://<your-url>/` loads; the header pill says **connected**.
- [ ] Choose *1. Basic garbage*, press **Step** ×9 then through the GC: ends with objects `a` and `b` only.
- [ ] *2. Cycle* leaks under `rc`; change to `use marksweep` and it is reclaimed.
- [ ] Try it from your phone (the layout is responsive).

## 4. Day-2 operations

| Task | How |
|---|---|
| Deploy a change | `git add . && git commit -m "…" && git push` → Render rebuilds automatically (Auto-Deploy) |
| Roll back | Render dashboard → service → **Events** (or **Deploys**) → pick an earlier deploy → **Rollback** |
| See errors | **Logs** tab (the server prints startup info and request failures) |
| Restart / wipe state | **Manual Deploy → Restart** (the heap lives only in memory) |
| Custom domain | **Settings → Custom Domains** (HTTPS is automatic) |

## 5. How the free tier behaves (set expectations)

Render's free web services **spin down after about 15 minutes without traffic** and take **about a minute to wake up** on the
next visit; they have 512 MB RAM / 0.1 CPU and a monthly hour allowance, and the free plan has changed before — re-read
render.com/pricing before relying on it. HeapLens needs far less than 512 MB. Consequences:
- First visitor after idle sees a slow first load (this is normal; the page works once it wakes).
- Every spin-down/restart **empties the heap** — harmless for a demo.
- Optional: an external uptime pinger on `/healthz` every ~10 min keeps it awake (uses up the monthly hours; check Render's terms).

## 6. Known limitations of this first deployment (by design, for now)

- **One global runtime for everyone.** All visitors share the same heap; one person's *Restart* resets it for all.
- **No login, no rate limiting.** Anyone with the URL can run programs. The runtime bounds the damage (heap ≤ 64 KB, programs ≤ 3000
  statements, 5 s socket timeouts), but this is a demo setup — don't share it widely until phase 2.
- Single-threaded server: requests are handled one at a time (fast enough for a few simultaneous viewers).

## 7. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Build fails in the `RUN g++ …` step | Open the build log; send me the first error line. Check `include/`, `src/` were committed (`git ls-files | findstr src`). |
| Build fails: file not found for `COPY ui` | `ui/` missing from the repo, or excluded by `.dockerignore`; commit it. |
| Deploy "Live" but page says *server not reachable* | Open `/healthz` directly. If it fails, check Logs for the startup line `HeapLens UI: … (listening on 0.0.0.0)`. |
| "Port scan timeout" / no open port detected | The server must listen on `$PORT`; HeapLens does when `PORT` is set. Make sure the service type is **Web Service**, not Background Worker. |
| First load takes ~1 min or shows a 502 once | Cold start after idle — wait and refresh. |
| UI looks old after a deploy | Hard refresh (Ctrl+F5). |
| Free instance hours used up | Service suspends until next month; upgrade, or move to Hugging Face Spaces (`docs/deployment.md`, option B). |

## 8. Phase 2 — after the first deploy works

1. **Per-visitor sessions** — cookie → its own runtime, idle expiry (e.g. 30 min), cap on number of live sessions (memory).
2. **Hardening** — `POST` for commands, request-size limits, per-IP rate limit, optional basic auth for private demos.
3. **Observability** — request log with session id, a `/metrics` counter, uptime ping.
4. **Optional: WebAssembly build** — run the engine in the browser so it can be hosted as static files (GitHub Pages/Vercel), with
   per-visitor state for free. Needs Emscripten.

## 9. Time estimate
GitHub push 10 min · Render setup + first build 10–15 min · verification 5 min → **about 30 minutes**, plus fixing small surprises
from the untested Docker build.
