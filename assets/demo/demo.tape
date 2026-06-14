# khatm — the seal ceremony. The launch GIF.
#
# Record it (from the repo root, after `make`):
#     go install github.com/charmbracelet/vhs@latest   # or: brew install vhs
#     vhs assets/demo/demo.tape                         # writes assets/demo.gif
#
# vhs drives a real terminal, so the animation is captured exactly as a user
# sees it. The Hide/Show block seeds a throwaway data dir off-camera.

Output assets/demo.gif

Set Shell bash
Set FontSize 20
Set Width 1100
Set Height 660
Set Padding 24
Set Theme "Catppuccin Mocha"

Hide
Type "export PATH=$PWD:$PATH KHATM_BIN=khatm KHATM_DIR=$(mktemp -d) LANG=en_US.UTF-8" Enter
Type "source assets/demo/seed.sh && clear" Enter
Sleep 1s
Show

Type "khatm done ostep/7" Sleep 900ms Enter
Sleep 9s
