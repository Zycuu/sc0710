#!/usr/bin/env bash
set -euo pipefail

prefix="${1:-$HOME/.local}"
bin_dir="$prefix/bin"
app_dir="$prefix/share/applications"

mkdir -p "$bin_dir" "$app_dir"
cp sc0710-capture.py "$bin_dir/sc0710-capture"
chmod +x "$bin_dir/sc0710-capture"
cp sc0710-capture.desktop "$app_dir/sc0710-capture.desktop"
sed -i "s|Exec=sc0710-capture|Exec=$bin_dir/sc0710-capture|" "$app_dir/sc0710-capture.desktop"

echo "Installed sc0710-capture to $bin_dir"
echo "Install runtime dependencies with: sudo pacman -S --needed ffmpeg v4l-utils alsa-utils python"
