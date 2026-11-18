#!/bin/sh
# Answers the open questions of ADR 0003 (burrow-agent) on an Ubuntu desktop, before the agent is written.
# Run it in a terminal on the Ubuntu computer while you are logged in at its screen, and paste the output back.
# It locks and unlocks the screen and turns the monitor off and on for a few seconds; nothing is installed or
# changed for good. If an unlock step fails, unlock the screen yourself with your password as usual.
# Usage: sh agent-probe.sh
set -u

say() { printf '\n== %s\n' "$*"; }
# The graphical session of this user (the one Desktop Sharing shows).
sid=$(loginctl show-user "$(id -un)" -p Display --value 2>/dev/null)
locked() { loginctl show-session "$sid" -p LockedHint --value 2>/dev/null; }
# Runs a command the way a systemd user service (the agent) would: outside the graphical session.
asagent() { systemd-run --user --quiet --wait --pipe --collect "$@"; }
gnome_ss() { asagent gdbus call --session --dest org.gnome.ScreenSaver --object-path /org/gnome/ScreenSaver \
                 --method "org.gnome.ScreenSaver.$1" ${2:+"$2"}; }
power() { asagent gdbus call --session --dest org.gnome.Mutter.DisplayConfig --object-path /org/gnome/Mutter/DisplayConfig \
              --method org.freedesktop.DBus.Properties.Set org.gnome.Mutter.DisplayConfig PowerSaveMode "<int32 $1>"; }

say "System"
. /etc/os-release && echo "$PRETTY_NAME"
gnome-shell --version 2>&1
gnome-remote-desktop --version 2>/dev/null || dpkg-query -W gnome-remote-desktop 2>/dev/null
echo "session=$sid type=${XDG_SESSION_TYPE:-?}"
[ -n "$sid" ] || { echo "No graphical session found for this user; run this in a terminal on the Ubuntu desktop."; exit 1; }
loginctl show-session "$sid" -p Type -p Active -p LockedHint -p Remote -p Seat

say "Remote desktop settings (no passwords are printed)"
grdctl status 2>&1 | grep -viE 'password|credential' | head -20
systemctl --user is-active gnome-remote-desktop 2>&1

say "Who may lock and unlock sessions (polkit)"
pkaction --verbose --action-id org.freedesktop.login1.lock-sessions 2>/dev/null | grep -iE 'implicit'

say "Test 1: lock the screen from a user service"
asagent loginctl lock-session "$sid"; echo "rc=$?"
sleep 4
echo "locked=$(locked)"

say "Test 2: unlock it from a user service (loginctl unlock-session)"
asagent loginctl unlock-session "$sid"; echo "rc=$?"
sleep 3
echo "locked=$(locked)"

if [ "$(locked)" = "yes" ]; then
    say "Test 3: unlock through GNOME's screensaver interface instead (SetActive false)"
    gnome_ss SetActive false; echo "rc=$?"
    sleep 3
    echo "locked=$(locked)"
fi

say "Test 4: lock again with GNOME's own screensaver interface, then unlock with loginctl"
gnome_ss Lock; echo "rc=$?"
sleep 4
echo "locked=$(locked)"
asagent loginctl unlock-session "$sid"; echo "rc=$?"
sleep 3
echo "locked=$(locked)"

say "Test 5: monitor off for 5 seconds (Mutter PowerSaveMode 3), then on"
power 3; echo "rc=$?"
sleep 5
power 0; echo "rc=$?"

printf '\n== Optional test 6: does screen sharing keep showing the desktop while the monitor is off?\n'
printf 'Press Enter, then within 60 seconds connect to this computer from Burrow and watch the picture\n'
printf '(the monitor here stays off for 60 seconds). Or press Ctrl+C to skip.\n'
read -r _ || exit 0
power 3; echo "rc=$?"
sleep 60
power 0; echo "rc=$?"
echo "Did Burrow show the desktop while the monitor was off? Write yes or no below the output."

say "Done. Final state: locked=$(locked)"
