:- module(find_unique_n, [find_unique_n/4]).

:- meta_predicate find_unique_n(?,?,:,?).

:- use_module(library(ordsets)).
:- use_module(library(lists)).

find_unique_n(N, Term, Goal, Solutions) :-
    N > 0,
    (   retractall(unique_solutions(_)),
        assertz(unique_solutions([])),
        once((
            call(Goal),
            retract(unique_solutions(_current_set)),
            ord_union(_current_set, [Term], _next_set),
            length(_next_set, _len),
            assertz(unique_solutions(_next_set)),
            _len =:= N
        )),
        fail
    ;   retract(unique_solutions(Solutions))
    ).

