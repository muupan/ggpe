:- use_module(library(lists)).
:- use_module(library(maplist)).

gdl_distinct(_x, _y) :- _x \= _y.

gdl_or(_x, _y) :- _x; _y.
gdl_or(_x, _y, _z) :- _x; _y; _z.
gdl_or(_x, _y, _z, _w) :- _x; _y; _z; _w.
gdl_or(_x, _y, _z, _w, _v) :- _x; _y; _z; _w; _v.
gdl_or(_x, _y, _z, _w, _v, _q) :- _x; _y; _z; _w; _v; _q.
gdl_or(_x, _y, _z, _w, _v, _q, _r) :- _x; _y; _z; _w; _v; _q; _r.
gdl_or(_x, _y, _z, _w, _v, _q, _r, _s) :- _x; _y; _z; _w; _v; _q; _r; _s.
gdl_or(_x, _y, _z, _w, _v, _q, _r, _s, _t) :- _x; _y; _z; _w; _v; _q; _r; _s; _t.
gdl_or(_x, _y, _z, _w, _v, _q, _r, _s, _t, _u) :- _x; _y; _z; _w; _v; _q; _r; _s; _t; _u.

gdl_not(_x) :- not(_x).

:- dynamic gdl_true/1, gdl_does/2.

assert_true(_fact) :-
    assertz(gdl_true(_fact)).

assert_all_true([]).
assert_all_true([_h | _t]) :-
    assert_true(_h),
    assert_all_true(_t).

assert_does(_role, _action) :-
    assertz(gdl_does(_role, _action)).

assert_all_does([]).
assert_all_does([[_r, _a] | _t]) :-
    assert_does(_r, _a),
    assert_all_does(_t).

retractall_true :-
    retractall(gdl_true(_)).

retractall_does :-
    retractall(gdl_does(_, _)).
    
run_with_facts(_facts, _goal) :-
    assert_all_true(_facts),
    call(_goal) -> retractall_true; (retractall_true, fail).

run_with_facts_and_actions(_facts, _actions, _goal) :-
    assert_all_true(_facts),
    assert_all_does(_actions),
    call(_goal) -> (retractall_true, retractall_does); (retractall_true, retractall_does, fail).

% Usage:
%   ?- state_role(X).
%   X = [white, black]
state_role(_roles) :-
    all(_role, gdl_role(_role), _roles).

% Usage:
%   ?- state_init(X).
%   X = [cell('1','1',b),...,control(white)]
state_init(_facts) :-
    all(_fact, gdl_init(_fact), _facts).
    
state_next_without_assertion(_result) :-
    all(_fact, gdl_next(_fact), _facts) -> _result = _facts; _result = [].

state_next_and_goal_without_assertion(_facts, _role_goal_pairs) :-
    state_next_without_assertion(_facts),
    retractall_true,
    assert_all_true(_facts),
    (state_terminal_without_assertion -> state_goal_without_assertion(_role_goal_pairs); _role_goal_pairs = []).

% Usage:
%   ?- state_next([cell('1','1',b),...,control(white)],[[white,mark('1','1')],[black,noop]],X).
%   X = [cell('1','1',x),...,control(black)]
state_next(_oldfacts, _actions, _facts) :-
    run_with_facts_and_actions(_oldfacts, _actions, state_next_without_assertion(_facts)).
    
state_next_and_goal(_oldfacts, _actions, _facts, _role_goal_pairs) :-
    run_with_facts_and_actions(_oldfacts, _actions, state_next_and_goal_without_assertion(_facts, _role_goal_pairs)).
    
state_legal_without_assertion(_role_actions_pairs) :-
    all([_role, _actions], setof(_action, gdl_legal(_role, _action), _actions), _role_actions_pairs).

% Usage:
%   ?- state_legal([cell('1','1',b),...,control(white)],X).
%   X = [[white,[mark('1','1'),...,mark('3','3')]],[black,[noop]]]
state_legal(_facts, _role_actions_pairs) :-
    run_with_facts(_facts, state_legal_without_assertion(_role_actions_pairs)).

state_terminal_without_assertion :-
    gdl_terminal.

% Usage:
%   ?- state_terminal([cell('1','1',x),...,control(white)]).
%   yes/no
state_terminal(_facts) :-
    run_with_facts(_facts, state_terminal_without_assertion).

state_goal_without_assertion(_role_goal_pairs) :-
    all([_role, _goal], gdl_goal(_role, _goal), _role_goal_pairs).

% Usage:
%   ?- state_goal([cell('1','1',x),...,control(white)],X).
%   X = [[white,'100'],[black,'0']]
state_goal(_facts, _role_goal_pairs) :-
    run_with_facts(_facts, state_goal_without_assertion(_role_goal_pairs)).

% Select random item (_item) in list (_list)
% Usage:
%   ?- random_item([a,b,c,d,e],X).
%   X = c
random_item(_list, _item) :-
    length(_list, _list_size),
    _index is random(_list_size),
    nth0(_index, _list, _item).

% Select random action
% Usage:
%   ?- random_action([[white,[a1,a2,a3]],[black,[b1,b2,b3]]],X).
%   X = [[white,a2],[black,b1]]
random_action([_role, _actions], [_role, _action]) :-
    random_item(_actions, _action).

random_next_state_without_assertion(_next_facts) :-
    state_legal_without_assertion(_role_actions_pairs),
    maplist(random_action, _role_actions_pairs, _role_action_pairs),
    assert_all_does(_role_action_pairs),
    state_next_without_assertion(_next_facts),
    retractall_does.

% Select random action from state (_facts) and get next state (_next_facts)
% Usage:
%   ?- random_next_state([cell('1','1',b),...,control(white)],X).
%   X = [cell('1','1',x),...,control(black)]
random_next_state(_facts, _next_facts) :-
    run_with_facts(_facts, random_next_state_without_assertion(_next_facts)).
    
state_simulate_without_assertion(_role_goal_pairs) :-
    state_terminal_without_assertion ->
        state_goal_without_assertion(_role_goal_pairs);
        (
            random_next_state_without_assertion(_next_facts),
            retractall_true,
            assert_all_true(_next_facts),
            state_simulate_without_assertion(_role_goal_pairs)
        ).

% Do random simulation from state (_facts) to terminal state and get goals (_role_goal_pairs)
% Usage:
%   ?- state_simulate([cell('1','1',b),...,control(white)],X).
%   X = [[white,'100'],[black,'0']]
state_simulate(_facts, _role_goal_pairs) :-
    run_with_facts(_facts, state_simulate_without_assertion(_role_goal_pairs)).

state_simulate_with_history_without_assertion(_role_goal_pairs, _history_so_far, _history) :-
    state_terminal_without_assertion ->
        (state_goal_without_assertion(_role_goal_pairs), _history_so_far = _history);
        (
            random_next_state_without_assertion(_next_facts),
            retractall_true,
            assert_all_true(_next_facts),
            append([_next_facts], _history_so_far, _temp_history),
            state_simulate_with_history_without_assertion(_role_goal_pairs, _temp_history, _history)
        ).

state_simulate_with_history(_facts, _role_goal_pairs, _history) :-
    assert_all_true(_facts),
    state_simulate_with_history_without_assertion(_role_goal_pairs, [_facts], _history),
    retractall_true.

state_base(_facts) :-
    memo_state_base(_facts), !.
state_base(_facts) :-
    all(_fact, gdl_base(_fact), _facts),
    assertz(memo_state_base(_facts)).

state_input(_role_actions_pairs) :-
    memo_state_input(_role_actions_pairs), !.
state_input(_role_actions_pairs) :-
    all([_role, _actions], setof(_action, gdl_input(_role, _action), _actions), _role_actions_pairs),
    assertz(memo_state_input(_role_actions_pairs)).

state_input_only_actions(_actions) :-
    memo_state_input_only_actions(_actions), !.
state_input_only_actions(_actions) :-
    all(_action, gdl_input(_, _action), _actions),
    assertz(memo_state_input_only_actions(_actions)).
    
unordered_domain(_relation, _list) :-
    all(_x, (call(_relation, _x, _); call(_relation, _, _x)), _list).
    
lower_bound(_relation, _first) :-
    call(_relation, _first, _),
    not(call(_relation, _, _first)).
    
upper_bound(_relation, _first) :-
    call(_relation, _, _first),
    not(call(_relation, _first, _)).
    
ordered_domain_tmp(_relation, [], _domain) :-
    upper_bound(_relation, _x),
    ordered_domain_tmp(_relation, [_x], _domain).
ordered_domain_tmp(_relation, [_h | _t], _domain) :-
    call(_relation, _x, _h),
    ordered_domain_tmp(_relation, [_x, _h | _t], _domain).
ordered_domain_tmp(_relation, [_h | _t], _domain) :-
    lower_bound(_relation, _h),
    remove_duplicates([_h | _t], _domain),
    unordered_domain(_relation, _unordered),
    same_length(_unordered, _domain),
    length(_domain, _length),
    _length >= 3.
    
% Usage:
%   ?- ordered_domain(gdl_succ,X).
%   X = [a_1,a_2,a_3,...]
ordered_domain(_relation, _ordered_domain) :-
    memo_state_ordered_domain(_order_relation_domain_pairs), !,
    member(_order_relation_domain_pair, _order_relation_domain_pairs),
    _order_relation_domain_pair = [_relation, _ordered_domain].
ordered_domain(_relation, _ordered_domain) :-
    user_defined_functor(_relation, 2),
    not(non_ground(_relation)),
    all(_domain, ordered_domain_tmp(_relation, [], _domain), _domains),
    length(_domains, 1),
    [_ordered_domain] = _domains.
    
nested_call(_outside, _inside, _arg) :-
    _goal_inside =.. [_inside ,_arg],
    _goal_outside =.. [_outside, _goal_inside],
    call(_goal_outside).
    
initial_value(_relation, _value) :-
    nested_call(gdl_init, _relation, _value).

step_counter(_fact_relation, _order_relation) :-
    initial_value(_fact_relation, _initial_value),
    call(_order_relation, _initial_value, _next_value),
    _initial_step_fact =.. [_fact_relation, _initial_value],
    state_next([_initial_step_fact],[],_next_facts),
    _next_step_fact =.. [_fact_relation, _next_value],
    member(_next_step_fact, _next_facts).

fact_and_relation(_relation, _fact) :-
    _fact =.. [_relation | _], !.
fact_and_relation(_relation, _fact) :-
    atom(_fact),
    _relation = _fact.

extract_facts(_fact_rel, _facts, _rel_facts) :-
    all(_rel_fact, (member(_rel_fact, _facts), fact_and_relation(_fact_rel, _rel_fact)), _rel_facts).

extract_one_fact(_fact_relation, _facts, _fact_of_given_relation) :-
    extract_facts(_fact_relation, _facts, _facts_of_given_relation),
    length(_facts_of_given_relation, _size),
    [_fact_of_given_relation] = _facts_of_given_relation.

fact_history_tmp(_fact_relation, _history, [_current_relation_fact | _t]) :-
    not(member(_current_relation_fact, _t)), % Avoid loop
    state_next([_current_relation_fact], [], _next_facts),
    extract_facts(_fact_relation, _next_facts, _next_relation_facts),
    length(_next_relation_facts, _next_relation_fact_count),
    _next_relation_fact_count = 1 ->
    (
        [_next_relation_fact] = _next_relation_facts,
        fact_history_tmp(_fact_relation, _history, [_next_relation_fact, _current_relation_fact | _t])
    );
    (
        _next_relation_fact_count = 0,
        reverse([_current_relation_fact | _t], _history)
    ).

fact_history(_fact_relation, _history) :-
    state_init(_initial_facts),
    extract_one_fact(_fact_relation, _initial_facts, _initial_relation_fact),
    fact_history_tmp(_fact_relation, _history, [_initial_relation_fact]),
    length(_history, _size),
    state_role(_roles),
    length(_roles, _role_size),
    _size > _role_size.

step_counter(_fact_relation, _order_relation) :-
    fact_history(_fact_relation, _history),
    ordered_domain(_order_relation, _domain),
    maplist(arg(1), _history, _values),
    sublist(_values, _domain).

step_counter(_step_relation) :-
    order_relation(_order_relation),
    user_defined_functor(_step_relation, 1),
    fact_relation(_step_relation),
    step_counter(_step_relation, _order_relation).

state_step_counter(_step_relations) :-
    all(_step_relation, step_counter(_step_relation), _step_relations).
    
:- initialization(state_ordered_domain(_)).
    
state_ordered_domain(_order_relation_domain_pairs) :-
    memo_state_ordered_domain(_order_relation_domain_pairs), !.
state_ordered_domain(_order_relation_domain_pairs) :-
    all([_order_relation, _domain], (user_defined_functor(_order_relation, 2), ordered_domain(_order_relation, _domain)), _order_relation_domain_pairs),
    assertz(memo_state_ordered_domain(_order_relation_domain_pairs)).

equivalent_args_bidir(_rel1, _arg1, _rel2, _arg2) :-
    equivalent_args(_rel1, _arg1, _rel2, _arg2);
    equivalent_args(_rel2, _arg2, _rel1, _arg1).
    
connected_args_bidir(_rel1, _arg1, _rel2, _arg2) :-
    connected_args(_rel1, _arg1, _rel2, _arg2);
    connected_args(_rel2, _arg2, _rel1, _arg1).

directly_connected_args(gdl_distinct, 1, gdl_distinct, 2).
directly_connected_args(gdl_distinct, 2, gdl_distinct, 1).
directly_connected_args(_order_rel, 1, _order_rel, 2) :-
    ordered_domain(_order_rel, _).
directly_connected_args(_order_rel, 2, _order_rel, 1) :-
    ordered_domain(_order_rel, _).
%directly_connected_args(_rel1, _arg1, _rel2, _arg2, []) :-
%    write('d1'),print([_rel1, _arg1, _rel2, _arg2]),nl,
%    equivalent_args_bidir(_rel1, _arg1, _rel2, _arg2).
%directly_connected_args(_rel1, _arg1, _rel2, _arg2, []) :-
%    write('d2'),print([_rel1, _arg1, _rel2, _arg2]),nl,
%    connected_args_bidir(_rel1, _arg1, _rel2, _arg2).
%directly_connected_args(_rel1, _arg1, _rel3, _arg3, [[_rel2, _arg2] | _path]) :-
%    write('d3'),print([_rel1, _arg1, _rel3, _arg3]),nl,
%    equivalent_args_bidir(_rel1, _arg1, _rel2, _arg2),
%    not(member([_rel2, _arg2], _path)),
%    directly_connected_args(_rel2, _arg2, _rel3, _arg3, _path).
%directly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
%    write('d4'),print([_rel1, _arg1, _rel2, _arg2]),nl,
%    directly_connected_args(_rel1, _arg1, _rel2, _arg2, _).
directly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
    equivalent_args(_rel1, _arg1, _rel2, _arg2).
directly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
    connected_args(_rel1, _arg1, _rel2, _arg2).
%directly_connected_args(_rel1, _arg1, _rel3, _arg3) :-
%    connected_args_bidir(_rel1, _arg1, _rel2, _arg2),
%    functor_arg_index(_rel3, _arg3),
%    (
%        equivalent_connection_path(_rel2, _arg2, _rel3, _arg3, []);
%        equivalent_connection_path(_rel3, _arg3, _rel2, _arg2, [])
%    ).

equivalent_connection_path(_, _, _, _, _path, _) :-
    length(_path, _length),
    _length > 5,
    !, fail.
equivalent_connection_path(_rel, _arg, _rel, _arg, _path, _ans) :-
    %functor_arg_index(_rel, _arg),
    %write('e1'),print([_rel, _arg, _rel, _arg, _path, _ans]),nl,
    reverse([[_rel, _arg] | _path], _ans).
    %write('ans'),print(_ans),nl.
equivalent_connection_path(_rel1, _arg1, _rel2, _arg2, _path, _ans) :-
    %write('e2'),print([_rel1, _arg1, _rel2, _arg2, _path, _ans]),nl,
    not(member([_rel1, _arg1], _path)),
    equivalent_args(_rel1, _arg1, _reltmp, _argtmp),
    equivalent_connection_path(_reltmp, _argtmp, _rel2, _arg2, [[_rel1, _arg1] | _path], _ans).
    
directly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
    equivalent_connection_path(_rel1, _arg1, _rel2, _arg2, [], _).

directly_connected_args_bidir(_rel1, _arg1, _rel2, _arg2) :-
    memo_directly_connected_args_all(_all), !,
    member([_rel1, _arg1, _rel2, _arg2], _all).
    
directly_connected_args_bidir(_rel1, _arg1, _rel2, _arg2) :-
    [_rel1, _arg1] \== [_rel2, _arg2],
    (
        once(directly_connected_args(_rel1, _arg1, _rel2, _arg2));
        once(directly_connected_args(_rel2, _arg2, _rel1, _arg1))
    ).
    

directly_connected_args_all(_all) :-
    memo_directly_connected_args_all(_all), !.
directly_connected_args_all(_all) :-
    all([_rel1,_arg1,_rel2,_arg2], (functor_arg_index(_rel1,_arg1), functor_arg_index(_rel2,_arg2), [_rel1, _arg1] \== [_rel2, _arg2], directly_connected_args_bidir(_rel1,_arg1,_rel2,_arg2)), _all),
    assertz(memo_directly_connected_args_all(_all)).
    
indirectly_connected_args_all(_all) :-
    memo_indirectly_connected_args_all(_all), !.
indirectly_connected_args_all(_all) :-
    all([_rel1,_arg1,_rel2,_arg2], (functor_arg_index(_rel1,_arg1), functor_arg_index(_rel2,_arg2), [_rel1, _arg1] \== [_rel2, _arg2], indirectly_connected_args(_rel1,_arg1,_rel2,_arg2)), _all),
    assertz(memo_indirectly_connected_args_all(_all)).
    
%:- initialization(directly_connected_args_all(_)).
%:- initialization(indirectly_connected_args_all(_)).

indirectly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
    memo_indirectly_connected_args_all(_all), !,
    member([_rel1, _arg1, _rel2, _arg2], _all).
    
%    once((
%        equivalent_connection_path(_rel1, _arg1, _rel2, _arg2, [], _);
%        equivalent_connection_path(_rel2, _arg2, _rel1, _arg1, [], _)
%    )).
    
direct_connection_path(_, _, _, _, _path, _) :-
    length(_path, _length),
    _length > 2,
    !, fail.
direct_connection_path(_rel, _arg, _rel, _arg, _path, _ans) :-
    write('dc1'),print([_rel, _arg, _rel, _arg, _path, _ans]),nl,
    reverse([[_rel, _arg] | _path], _ans).
direct_connection_path(_rel1, _arg1, _rel2, _arg2, _path, _ans) :-
    %write('dc2'),print([_rel1, _arg1, _rel2, _arg2, _path, _ans]),nl,
    not(member([_rel1, _arg1], _path)),
    directly_connected_args_bidir(_rel1, _arg1, _reltmp, _argtmp),
    direct_connection_path(_reltmp, _argtmp, _rel2, _arg2, [[_rel1, _arg1] | _path], _ans).
    
indirectly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
    write('ic1'),print([_rel1, _arg1, _rel2, _arg2]),nl,
    directly_connected_args_bidir(_rel1, _arg1, _rel2, _arg2), !.
indirectly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
    write('ic2'),print([_rel1, _arg1, _rel2, _arg2]),nl,
    direct_connection_path(_rel1, _arg1, _rel2, _arg2, [], _).
    
%indirectly_connected_args_bidir(_rel1, _arg1, _rel2, _arg2) :-
%    indirectly_connected_args(_rel1, _arg1, _rel2, _arg2);
%    indirectly_connected_args(_rel2, _arg2, _rel1, _arg1).
    
list_lt([], [_]).
list_lt([_ah | _at], [_bh | _bt]) :-
    _ah < _bh, !.
list_lt([_ah | _at], [_bh | _bt]) :-
    _ah = _bh,
    list_lt(_at, _bt).

atom_lt(_a, _b) :-
    atom_codes(_a, _a_chars),
    atom_codes(_b, _b_chars),
    list_lt(_a_chars, _b_chars).
    
%indirectly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
%    (atom_lt(_rel2, _rel1); (_rel1 = _rel2, _arg2 < _arg1)),
%    indirectly_connected_args(_rel2, _arg2, _rel1, _arg1).
%indirectly_connected_args(gdl_distinct, 1, gdl_distinct, 2).
%indirectly_connected_args(_order_rel, 1, _order_rel, 2) :-
%    ordered_domain(_order_rel, _).
%indirectly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
%    directly_connected_args(_rel1, _arg1, _rel2, _arg2).
%indirectly_connected_args(_rel1, _arg1, _rel3, _arg3) :-
%    (atom_lt(_rel1, _rel3); (_rel1 = _rel3, _arg1 < _arg3)),
%    directly_connected_args(_rel1, _arg1, _rel2, _arg2),
%    indirectly_connected_args(_rel2, _arg2, _rel3, _arg3).

    
%indirectly_connected_args(_rel1, _arg1, _rel2, _arg2, []) :-
%    write('i1'),print([_rel1, _arg1, _rel2, _arg2]),nl,
%    directly_connected_args(_rel1, _arg1, _rel2, _arg2).
%indirectly_connected_args(_rel1, _arg1, _rel3, _arg3, [[_rel2, _arg2] | _path]) :-
%    write('i2'),print([_rel1, _arg1, _rel3, _arg3, _path]),nl,
%    directly_connected_args(_rel1, _arg1, _rel2, _arg2),
%    indirectly_connected_args(_rel2, _arg2, _rel3, _arg3, _path),
%    print([_rel2, _arg2]),nl,
%    not(member([_rel2, _arg2], _path)).
%    %_new_path = [[_rel2, _arg2] | _path].
%indirectly_connected_args(_rel1, _arg1, _rel2, _arg2) :-
%    indirectly_connected_args(_rel1, _arg1, _rel2, _arg2, _).
    
fact_arg_values_triplets(_fact_arg_values_triplets) :-
    memo_fact_arg_values_triplets(_fact_arg_values_triplets), !.
fact_arg_values_triplets(_fact_arg_values_triplets) :-
    state_base(_facts),
    all([_fact_rel, _arg, _values], (fact_relation(_fact_rel), extract_facts(_fact_rel, _facts, _rel_facts), functor_arg_index(_fact_rel, _arg), all(_value, (member(_fact, _rel_facts), arg(_arg, _fact, _value)), _values)), _fact_arg_values_triplets),
    assertz(memo_fact_arg_values_triplets(_fact_arg_values_triplets)).
   
fact_arg_values(_fact_rel, _arg, _values) :-
    fact_arg_values_triplets(_fact_arg_values_triplets),
    member([_fact_rel, _arg, _values], _fact_arg_values_triplets).
% fact_arg_values(_fact_rel, _arg, _values) :-
%     state_base(_facts),
%     extract_facts(_fact_rel, _facts, _rel_facts),
%     all(_value, (member(_fact, _rel_facts), arg(_arg, _fact, _value)), _values).
    
fact_arg_ordered_domain(_fact_rel, _arg, _order_rel) :-
    ordered_domain(_order_rel, _domain),
    %print(_order_rel),write(' domain:'),print(_domain),nl,
    fact_arg_values(_fact_rel, _arg, _values),
    %print(_fact_rel),write(' arg:'),print(_arg),write(' values:'),print(_values),nl,
    forall(member(_value, _values), member(_value, _domain)).

values_are_in_ordered_domain(_values, _order_rel) :-
    % ordered_domain(_order_rel, _domain),
    state_ordered_domain(_order_rel_domain_pairs),
    member([_order_rel, _domain], _order_rel_domain_pairs),
    forall(member(_value, _values), member(_value, _domain)).
    
action_arg_values_triplets(_action_arg_values_triplets) :-
    memo_action_values_triplets(_action_arg_values_triplets), !.
action_arg_values_triplets(_action_arg_values_triplets) :-
    state_input_only_actions(_actions),
    all([_action_rel, _arg, _values], (action_relation(_action_rel), extract_actions(_action_rel, _actions, _rel_actions), functor_arg_index(_action_rel, _arg), all(_value, (member(_action, _rel_actions), arg(_arg, _action, _value)), _values)), _action_arg_values_triplets),
    assertz(memo_action_values_triplets(_action_arg_values_triplets)).

action_arg_values(_action_rel, _arg, _values) :-
    action_arg_values_triplets(_action_arg_values_triplets),
    member([_action_rel, _arg, _values], _action_arg_values_triplets).

% action_arg_values(_action_rel, _arg, _values) :-
%     state_input_only_actions(_actions),
%     %write('actions:'),print(_actions),nl,
%     extract_actions(_action_rel, _actions, _rel_actions),
%     %write('rel_actions:'),print(_rel_actions),nl,
%     all(_value, (member(_action, _rel_actions), arg(_arg, _action, _value)), _values).
    
action_arg_ordered_domain(_action_rel, _arg, _order_rel) :-
    ordered_domain(_order_rel, _domain),
    %print(_order_rel),write(' domain:'),print(_domain),nl,
    action_arg_values(_action_rel, _arg, _values),
    %print(_action_rel),write(' arg:'),print(_arg),write(' values:'),print(_values),nl,
    forall(member(_value, _values), member(_value, _domain)).

connected_fact_action_args(_fact_rel, _fact_arg, _action_rel, _action_arg, _order_rel) :-
    %write('connected_fact_action_args'),print([_fact_rel, _fact_arg, _action_rel, _action_arg, _order_rel]),nl,
    directly_connected_args_bidir(_fact_rel, _fact_arg, _action_rel, _action_arg),
    %writeln('a'),
    %(
    %    indirectly_connected_args(_fact_rel, _fact_arg, _order_rel, 1);
    %    indirectly_connected_args(_action_rel, _action_arg, _order_rel, 1)
    %),
    %writeln('a'),
    fact_arg_values(_fact_rel, _fact_args, _values),
    action_arg_values(_action_rel, _action_args, _values),
    values_are_in_ordered_domain(_values, _order_rel).
    % fact_arg_ordered_domain(_fact_rel, _fact_arg, _order_rel),
    % %writeln('a'),
    % action_arg_ordered_domain(_action_rel, _action_arg, _order_rel), !.
    % %writeln('connection found.'), !.

fact_relations(_fact_rels) :-
    memo_fact_relations(_fact_rels), !.
fact_relations(_fact_rels) :-
    state_base(_facts),
    all(_fact_rel, (member(_fact, _facts), fact_and_relation(_fact_rel, _fact)), _fact_rels),
    assertz(memo_fact_relations(_fact_rels)).
    
action_relations(_action_rels) :-
    memo_action_relations(_action_rels), !.
action_relations(_action_rels) :-
    state_input_only_actions(_actions),
    all(_action_rel, (member(_action, _actions), action_and_relation(_action_rel, _action)), _action_rels),
    assertz(memo_action_relations(_action_rels)).

fact_relation(_rel) :-
    fact_relations(_rels),
    member(_rel, _rels).
    
action_relation(_rel) :-
    action_relations(_rels),
    member(_rel, _rels).

all_fact_relation(_fact_relations) :-
    all(_fact_relation, (gdl_base(_fact), fact_and_relation(_fact_relation, _fact)), _fact_relations).
    
all_action_relation(_action_relations) :-
    all(_action_relation, (gdl_input(_, _action), action_and_relation(_action_relation, _action)), _action_relations).

order_relation(_order_rel) :-
    ordered_domain(_order_rel, _).

order_relations(_order_rels) :-
    all(_order_rel, ordered_relation(_order_rel), _order_rels).

functor_arg_index(_rel, _arg_index) :-
    user_defined_functor(_rel, _arg_count),
    between(1, _arg_count, _arg_index).

connected_fact_and_action(_fact_rel, _action_rel, _connection) :-
    fact_relation(_fact_rel),
    action_relation(_action_rel),
    %write('connected_fact_and_action'),print([_fact_rel, _action_rel, _order_rel]),nl,
    all([_order_rel, [_arg1, _arg2]], (functor_arg_index(_fact_rel, _arg1), functor_arg_index(_action_rel, _arg2), connected_fact_action_args(_fact_rel, _arg1, _action_rel, _arg2, _order_rel)), _connection).
    %write('connection:'),print(_connection),nl.

state_fact_action_connections(_connections) :-
    all([_fact_rel, _action_rel, _connection], connected_fact_and_action(_fact_rel, _action_rel, _connection), _connections).
    %print(_connections),nl.

fact_ordered_args(_fact_rel, _ordered_args) :-
    fact_relation(_fact_rel), 
    all([_arg, _order_rel], (functor_arg_index(_fact_rel, _arg), fact_arg_ordered_domain(_fact_rel, _arg, _order_rel)), _ordered_args).

state_fact_ordered_args(_fact_ordered_args_pairs) :-
    all([_fact_rel, _ordered_args], fact_ordered_args(_fact_rel, _ordered_args), _fact_ordered_args_pairs).
    
action_ordered_args(_action_rel, _ordered_args) :-
    action_relation(_action_rel), 
    all([_arg, _order_rel], (functor_arg_index(_action_rel, _arg), action_arg_ordered_domain(_action_rel, _arg, _order_rel)), _ordered_args).

state_action_ordered_args(_action_ordered_args_pairs) :-
    all([_action_rel, _ordered_args], action_ordered_args(_action_rel, _ordered_args), _action_ordered_args_pairs).

increment_former([_a, _b], [_aplus, _b]) :-
    plus(_a, 1, _aplus).

correspond([], _, []).
correspond([_a | _b], _c, [[1, _index] | _updated]) :-
    nth1(_index, _c, _a),
    correspond(_b, _c, _old),
    maplist(increment_former, _old, _updated).
correspond([_ah | _at], _b, _updated) :-
    not(nth1(_index, _b, _ah)),
    correspond(_at, _b, _old),
    maplist(increment_former, _old, _updated).
correspond(_a, _b, _c) :-
    correspondence(_b, _a, _reversed),
    maplist(reverse, _reversed, _c).
    
correspond_all(_a, _b, _cs) :-
    all(_c, correspond(_a, _b, _c), _cs).

action_and_relation(_action_relation, _action) :-
    _action =.. [_action_relation | _], !.
action_and_relation(_action_relation, _action) :-
    atom(_action),
    _action_relation = _action.

extract_actions(_action_rel, _actions, _rel_actions) :-
    all(_rel_action, (member(_rel_action, _actions), action_and_relation(_action_rel, _rel_action)), _rel_actions).

fact_action_pair(_fact, _role, _action, _cor) :-
    gdl_input(_role, _action),
    state_next([], [[_role, _action]], [_fact]),
    _fact =.. [_fact_rel | _fact_args],
    _action =.. [_action_rel | _action_args],
    correspond_all(_fact_args, _action_args, _cor).
    
fact_action_rel_pair(_fact_rel, _role, _action_rel, _cor) :-
    fact_action_pair(_fact, _role, _action, _cor),
    _fact =.. [_fact_rel | _],
    _action =.. [_action_rel | _].
    
remove_random_item(_list, _item, _rest) :-
    random_item(_list, _item),
    delete(_list, _item, _rest).

random_sampling_tmp(_n, _list, _sample_list, _tmp) :-
    length(_tmp, _tmp_size),
    _tmp_size = _n ->
        _sample_list = _tmp;
        (
            remove_random_item(_list, _new_sample, _new_list),
            random_sampling_tmp(_n, _new_list, _sample_list, [_new_sample | _tmp])
        ).
    
random_sampling(_n, _list, _sample_list) :-
    length(_list, _size),
    _size =< _n ->
        _list = _sample_list;
        random_sampling_tmp(_n, _list, _sample_list, []).

fact_action_rel_pair_actions(_fact_rel, _role, _actions, _cor) :-
    member(_sample_action, _actions),
    state_next([], [[_role, _sample_action]], [_fact]),
    _fact =.. [_fact_rel | _fact_args],
    _sample_action =.. [_action_rel | _action_args],
    correspond_all(_fact_args, _action_args, _cor).
    
fact_action_rel_pair_(_fact_rel, _role, _action_rel, _shared_cor) :-
    user_defined_functor(_action_rel, _),
    gdl_role(_role),
    all(_action, gdl_input(_role, _action), _actions),
    extract_actions(_action_rel, _actions, _actions_of_rel_all),
    random_sampling(10, _actions_of_rel_all, _actions_of_rel),
    user_defined_functor(_fact_rel, _),
    all(_cor, fact_action_rel_pair_actions(_fact_rel, _role, _actions_of_rel, _cor), _cors),
    shared_by_all(_shared_cor, _cors).
    
shared_by_all(_, []).
shared_by_all(_shared, [_list | _list_of_list]) :-
    member(_shared, _list),
    shared_by_all(_shared, _list_of_list).

fact_action_correspond(_fact_rel, _role, _action_rel, _unique_cor) :-
    user_defined_functor(_fact_rel, _),
    user_defined_functor(_action_rel, _),
    gdl_role(_role),
    all(_cor, fact_action_rel_pair(_fact_rel, _role, _action_rel, _cor), _cors),
    shared_by_all(_unique_cor, _cors).
    
fact_action_correspond_all(_pairs) :-
    all([_fact_rel, _role, _action_rel, _cor],
        (
            fact_action_correspond(_fact_rel, _role, _action_rel, _cor)
        ),
        (
            _pairs
        )).

select_action(_role_actions_pair, _role_action_pair) :-
    _role_actions_pair = [_role, _actions],
    member(_action, _actions),
    _role_action_pair = [_role, _action].

joint_actions(_role_actions_pairs, _all_joint_actions) :-
    all(_role_action_pairs, maplist(select_action, _role_actions_pairs, _role_action_pairs), _all_joint_actions).
    
joint_actions_(_role_actions_pairs, _role_action_pairs) :-
    maplist(select_action, _role_actions_pairs, _role_action_pairs).

added_fact(_old_facts, _new_facts, _added_fact) :-
    member(_added_fact, _new_facts),
    not(member(_added_fact, _old_facts)).

removed_fact(_old_facts, _new_facts, _removed_fact) :-
    member(_removed_fact, _old_facts),
    not(member(_removed_fact, _new_facts)).

facts_changes(_old_facts, _new_facts, _changes) :-
    all(_added_fact, added_fact(_old_facts, _new_facts, _added_fact), _added_facts),
    all(_removed_fact, removed_fact(_old_facts, _new_facts, _removed_fact), _removed_facts),
    _changes = [_added_facts, _removed_facts].

state_changes(_facts, _joint_action, [_joint_action, _changes]) :-
    state_next(_facts, _joint_action, _next_facts),
    facts_changes(_facts, _next_facts, _changes).

% Not complete
fact_action_correspond_(_facts, _changes_list) :-
    state_legal(_facts, _role_actions_pairs),
    joint_actions(_role_actions_pairs, _joint_actions),
    maplist(state_changes(_facts), _joint_actions, _changes_list).
    
