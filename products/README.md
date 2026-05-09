# Products

This directory defines what ships for a given build profile.

A product should bind together:

- one board
- the core service set
- optional programs
- ports/packages that belong in the distributive
- image and release expectations

The goal is to make "what system are we building?" explicit instead of spreading it across Meson lists and boot-time special cases.
