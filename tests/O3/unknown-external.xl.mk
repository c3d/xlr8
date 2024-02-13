O3/unknown-external.OPT=-O3 -nobuiltins
O3/unknown-external.FILTER=sed -e 's/_some_unknown_symbol/some_unknown_symbol/g' -e 's/JIT session error: Unresolved symbol:/Unresolved symbols:/g' -e 's/Symbols not found.*some_unknown_symbol.*/Unresolved symbols: some_unknown_symbol/g' | grep -v "Failed to materialize symbols"
O3/unknown-external.EXIT=1
