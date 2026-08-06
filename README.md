# Nairobi Streets

A top-down action game set in the streets of Nairobi.

## Run Locally

```bash
make run
```

## Build for Web (GitHub Pages)

### Prerequisites
- Emscripten SDK installed and activated

```bash
# Install emsdk (one-time)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh  # Add to your shell profile
```

### Build
```bash
make web
```

This creates `nairobi_streets.html`, `nairobi_streets.js`, `nairobi_streets.wasm`.

### Test Locally
```bash
make web-run
# Open http://localhost:8080/nairobi_streets.html
```

## Deploy to GitHub

### Option 1: Source Code Only
```bash
git init
git add .
git commit -m "Initial commit"
# Create repo on GitHub, then:
git remote add origin https://github.com/YOURUSER/YOURREPO.git
git push -u origin main
```

### Option 2: Auto-Deploy to GitHub Pages (Playable in Browser)
1. Push to GitHub (above)
2. Go to Settings → Pages → Source: "GitHub Actions"
3. Push to main branch - workflow builds and deploys automatically
4. Play at `https://YOURUSER.github.io/YOURREPO/`

## Controls
- **WASD** - Move
- **Mouse** - Aim
- **LMB** - Shoot
- **RMB** - Block/Dodge
- **Shift** - Sprint
- **E** - Interact
- **Tab** - Map
- **M** - Missions
- **P** - Pause