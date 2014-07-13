subseq0(List, List).
subseq0(List, Rest) :- subseq1(List, Rest).
subseq1([_|Tail], Rest) :- subseq0(Tail, Rest).
subseq1([Head|Tail], [Head|Rest]) :-subseq1(Tail, Rest).
