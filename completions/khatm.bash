# bash completion for khatm
# install: source this file, or drop it in /etc/bash_completion.d/
_khatm() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    local cmds="init books status next study log book goal done review cards \
edit pace shelf graph doctor dump help"

    if [ "$COMP_CWORD" -eq 1 ]; then
        COMPREPLY=( $(compgen -W "$cmds" -- "$cur") )
        return
    fi

    local root="${KHATM_DIR:-$HOME/.khatm}"
    local ids=""
    if [ -d "$root/books" ]; then
        ids=$(cd "$root/books" && ls -1 *.md 2>/dev/null | sed 's/\.md$//')
    fi

    case "${COMP_WORDS[1]}" in
        study|log|done|goal|review|cards|edit|pace|book)
            COMPREPLY=( $(compgen -W "$ids" -- "$cur") ) ;;
        *) ;;
    esac
}
complete -F _khatm khatm
