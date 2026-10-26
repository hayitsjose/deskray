#!/usr/bin/env bash
# Provision a Linux machine as a deskray streaming host (Sunshine).
# Re-runnable: every step checks before it changes anything.
set -euo pipefail

log() { printf '\e[1;36m==>\e[0m %s\n' "$*"; }
die() { printf '\e[1;31mxx\e[0m %s\n' "$*" >&2; exit 1; }
[[ $EUID -ne 0 ]] || die "Run as your normal user; the script calls sudo where needed."

# 1. Install Sunshine
if ! command -v sunshine >/dev/null; then
  if command -v pacman >/dev/null; then
    if ! grep -q '^\[lizardbyte\]' /etc/pacman.conf; then
      log "Adding LizardByte pacman repo"
      printf '\n[lizardbyte]\nSigLevel = Optional\nServer = https://github.com/LizardByte/pacman-repo/releases/latest/download\n' \
        | sudo tee -a /etc/pacman.conf >/dev/null
    fi
    sudo pacman -Sy --needed sunshine
  elif command -v apt-get >/dev/null; then
    . /etc/os-release
    # Release assets: sunshine-ubuntu-24.04-amd64.deb, sunshine-debian-bookworm-amd64.deb
    if [[ $ID == debian ]]; then rel=$VERSION_CODENAME; else rel=$VERSION_ID; fi
    deb="sunshine-${ID}-${rel}-amd64.deb"
    curl -fL -o "/tmp/$deb" "https://github.com/LizardByte/Sunshine/releases/latest/download/$deb"
    sudo apt-get install -y "/tmp/$deb"
  elif command -v dnf >/dev/null; then
    sudo dnf copr enable -y lizardbyte/stable
    sudo dnf install -y Sunshine
  else
    die "Unsupported distro: install Sunshine by hand, then re-run."
  fi
fi

# 2. Input injection: /dev/uinput for this user
id -nG | grep -qw input || { log "Adding $USER to input group"; sudo usermod -aG input "$USER"; }
rule=/etc/udev/rules.d/60-sunshine.rules
if [[ ! -f $rule ]]; then
  echo 'KERNEL=="uinput", SUBSYSTEM=="misc", OPTIONS+="static_node=uinput", TAG+="uaccess"' | sudo tee "$rule" >/dev/null
  sudo udevadm control --reload-rules && sudo udevadm trigger
fi

# 3. Capture backend
case "${XDG_CURRENT_DESKTOP:-}:${XDG_SESSION_TYPE:-}" in
  *Hyprland*|*sway*|*wlroots*) capture=wlr ;;
  *KDE*:wayland)              capture=kwin ;;
  *:x11)                      capture=x11 ;;
  *)                          capture=kms
                              sudo setcap cap_sys_admin+p "$(readlink -f "$(command -v sunshine)")" ;;
esac

# 4. Encoder
if lspci | grep -qiE 'vga.*nvidia|3d.*nvidia'; then encoder=nvenc; else encoder=vaapi; fi

# 5. Config (keeps an existing file; delete it to regenerate)
conf="$HOME/.config/sunshine/sunshine.conf"
mkdir -p "$(dirname "$conf")"
if [[ ! -f $conf ]]; then
  cat >"$conf" <<EOF
sunshine_name = $(hostname)
capture = $capture
encoder = $encoder
origin_web_ui_allowed = pc
address_family = both
EOF
  log "Wrote $conf (capture=$capture encoder=$encoder)"
fi

# 6. Service
systemctl --user daemon-reload
systemctl --user enable --now sunshine.service

# 7. Firewall: print, don't touch (every machine's ruleset differs)
cat <<'EOF'

If your firewall doesn't already trust tailscale0, add to your nftables input
chain (before any final drop), then reload:
  iifname "tailscale0" tcp dport { 47984, 47989, 48010 } accept
  iifname "tailscale0" udp dport 47998-48000 accept

Next: open https://localhost:47990 on this machine and create the admin login.
EOF
