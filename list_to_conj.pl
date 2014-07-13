list_to_conj([H], H) :- !.
list_to_conj([H | T], ','(H, Conj)) :-
    list_to_conj(T, Conj).

conj_to_list(','(H, Conj), [H | T]) :-
      !, conj_to_list(Conj, T).
conj_to_list(H, [H]).

