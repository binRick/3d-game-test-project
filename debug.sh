#!/bin/bash
# Tail the game's debug log. Open this in a second Terminal tab while the
# game is running to watch player/enemy state at 5 Hz. Copy chunks from it
# into a message to describe pathing/AI bugs precisely.
LOG=/tmp/ironfist-debug.log
if [[ ! -e "$LOG" ]]; then
    echo "Waiting for $LOG — launch the game first."
    until [[ -e "$LOG" ]]; do sleep 0.3; done
fi
exec tail -F "$LOG"
