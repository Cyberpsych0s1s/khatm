# fish completion for khatm
# install: drop in ~/.config/fish/completions/khatm.fish
function __khatm_books
    set -l root $KHATM_DIR
    test -z "$root"; and set root $HOME/.khatm
    if test -d $root/books
        for f in $root/books/*.md
            basename $f .md
        end
    end
end

set -l cmds init books status next study log book goal done review cards edit pace shelf graph doctor dump help

complete -c khatm -f
complete -c khatm -n __fish_use_subcommand -a "$cmds"
complete -c khatm -n "__fish_seen_subcommand_from study log done goal review cards edit pace book" \
    -a "(__khatm_books)"
