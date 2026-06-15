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
