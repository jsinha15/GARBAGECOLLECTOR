# Deploying HeapLens for free

> **Chosen path: Render.** Follow the step-by-step plan in [../DEPLOY-RENDER.md](../DEPLOY-RENDER.md). The rest of this page compares the alternatives.

HeapLens is **one native program** that serves the web UI *and* the API. To deploy it you need somewhere that can
(a) compile or run that program and (b) expose one HTTP port. Do you need a Dockerfile? **Only on platforms that run
containers.** On a plain virtual machine, or from your own PC through a tunnel, you do not.

| Where | Dockerfile needed? | Cost / limits (checked mid-2026 — re-check, they change) | Best for |
|---|---|---|---|
| **A. Your PC + a tunnel** | no | free; only online while your PC runs it | showing it to someone *today* |
| **B. Hugging Face Spaces (Docker)** | **yes** | free CPU Space (2 vCPU / 16 GB); sleeps when idle | a permanent public demo link |
| **C. Render free web service** | **yes** | free; spins down after 15 min idle, ~1 min cold start; 512 MB RAM, 750 h/month | easiest "connect GitHub, done" |
| **D. Oracle Cloud Always Free VM** | **no** | free VM that never sleeps (AMD micro 1 GB, or ARM 2 OCPU/12 GB since June 2026); needs a card for sign-up verification | an always-on server you control |

All four use the **same code changes already in the repo**: the server reads the platform's `$PORT`, binds `0.0.0.0` when
`$PORT` is set (or with `--host 0.0.0.0`), and answers `GET /healthz` with `ok`.

> **Important (known limitation):** the runtime state is *global*. Everyone who opens your URL shares one heap, and anyone
> can press Restart for everybody. There is no login. That is fine for a demo; per-visitor sessions come later.
> Also: free tiers sleep or restart, and a restart empties the heap — just press Restart.

---

## A. Share from your own PC (no deployment, no Docker)

1. `start.bat` (or `build\minigc.exe --serve`) — HeapLens is now on http://127.0.0.1:8080/.
2. Open a tunnel to it. With Cloudflare's quick tunnel (needs the free `cloudflared` download, no account):
   ```
   cloudflared tunnel --url http://localhost:8080
   ```
   It prints a public `https://….trycloudflare.com` address. `ngrok http 8080` works the same way (free account).
3. Send the link. Close the tunnel to stop sharing.

## B. Hugging Face Spaces (Docker Space) — free permanent link

1. Create an account at huggingface.co → **New Space** → SDK **Docker** → *Blank* → hardware **CPU basic (free)** → public.
2. The Space is a git repository. Push this project to it (Space page → *Files* → clone URL), **plus a header at the very top of
   the Space's `README.md`**:
   ```
   ---
   title: HeapLens
   emoji: ♻️
   sdk: docker
   app_port: 7860
   ---
   ```
   (Keep this header only in the Space's copy of the README, not in your GitHub README.)
3. The repo's `Dockerfile` already sets `PORT=7860`, which is the port Spaces expects. The Space builds the image (a few
   minutes) and then serves `https://<user>-<space>.hf.space`.
4. If it sleeps after inactivity, the first visit wakes it.

## C. Render (free web service)

1. Push the project to a GitHub repository (include `Dockerfile`, `ui/`, `src/`, `include/`, `render.yaml`).
2. render.com → **New + → Blueprint** → select the repo. (Or **New + → Web Service → Docker** and choose the *Free* instance type.)
3. Render injects `PORT`; HeapLens picks it up automatically, and `render.yaml` points the health check at `/healthz`.
4. You get `https://heaplens-xxxx.onrender.com`. After 15 minutes without visitors it spins down; the next visit takes about a
   minute to wake it. Render has revised its free plan before — read the current page before relying on it.

## D. Oracle Cloud Always Free VM (no Docker, never sleeps)

1. Create an Oracle Cloud account (card required for verification, not charged for Always Free resources) and launch an
   **Always Free** Ubuntu VM (e.g. `VM.Standard.E2.1.Micro`, 1 GB — plenty for HeapLens).
2. In the VM's *Security List / Network Security Group* allow inbound TCP **8080** (or 80/443 if you add a proxy). On the VM itself:
   ```bash
   sudo iptables -I INPUT -p tcp --dport 8080 -j ACCEPT      # Ubuntu images block it by default
   ```
3. Build and run (no Docker; just a compiler):
   ```bash
   sudo apt update && sudo apt install -y g++ git
   git clone <your-repo-url> heaplens && cd heaplens
   ./build.sh
   PORT=8080 ./build/minigc --serve        # try it: http://<vm-public-ip>:8080/
   ```
4. Keep it running with systemd — `/etc/systemd/system/heaplens.service`:
   ```ini
   [Unit]
   Description=HeapLens
   After=network.target

   [Service]
   WorkingDirectory=/home/ubuntu/heaplens
   Environment=PORT=8080
   ExecStart=/home/ubuntu/heaplens/build/minigc --serve
   Restart=always

   [Install]
   WantedBy=multi-user.target
   ```
   ```bash
   sudo systemctl daemon-reload && sudo systemctl enable --now heaplens
   ```
5. Optional HTTPS: install Caddy and give it a free hostname (e.g. from DuckDNS):
   ```
   heaplens.duckdns.org {
       reverse_proxy localhost:8080
   }
   ```

---

## Before you share the link — checklist
- [ ] `GET /healthz` returns `ok` (`curl https://your-host/healthz`).
- [ ] Open the URL: the header pill says **connected**; run example 1 with **Step**.
- [ ] You know the heap is shared and unauthenticated (see the note above).
- [ ] Free tiers can sleep; tell viewers the first load may take a minute.

## What was and wasn't tested
Tested on the build machine: the new `--host`, the `$PORT` handling and `/healthz`, and the UI against the server. **Not tested
here:** the `Dockerfile`, `render.yaml` and the hosting platforms themselves (no Docker or accounts were available), so expect to fix
small details on first deploy.

## Sources for the free-tier facts
- Render free web services: https://render.com/articles/platforms-with-a-real-free-tier-for-developers-in-2026
- Hugging Face Docker Spaces (port 7860, sleeping): https://tds.s-anand.net/2025-09/huggingface-spaces/
- Oracle Always Free limits: https://docs.oracle.com/en-us/iaas/Content/FreeTier/resourceref.htm
