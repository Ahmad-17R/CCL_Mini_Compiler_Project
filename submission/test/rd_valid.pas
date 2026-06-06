{ Full-featured valid Pascal program for RD parser testing }
program fulltest ( input, output ) ;

var i, j, k : integer ;
var x, y    : real ;
var data     : array [ 1 .. 20 ] of integer ;

function max ( a, b : integer ) : integer ;
begin
    if a > b then
        max := a
    else
        max := b
end ;

function power ( base : real ; exp : integer ) : real ;
var result : real ;
var count  : integer ;
begin
    result := 1.0 ;
    count  := 0 ;
    while count < exp do
    begin
        result := result * base ;
        count  := count + 1
    end ;
    power := result
end ;

procedure fill ( n : integer ) ;
var idx : integer ;
begin
    idx := 1 ;
    while idx <= n do
    begin
        data[idx] := idx * idx ;
        idx := idx + 1
    end
end ;

begin
    { arithmetic }
    i := 5 ;
    j := 3 ;
    k := max(i, j) ;
    x := power(2.0, 10) ;
    y := x + 1.5 ;

    { array }
    fill(10) ;
    k := data[5] ;

    { logical expressions }
    if (i > 0) and (j > 0) then
        k := i + j
    else
        k := 0 ;

    if not (i = j) then
        i := i - 1
    else
        i := i + 1 ;

    { nested while }
    i := 10 ;
    while i > 0 do
    begin
        j := i mod 3 ;
        i := i - 1
    end

end.
