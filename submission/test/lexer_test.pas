{ Lexer / RD parser test — exercises every token category }
program lextest ( input, output ) ;

var i, j  : integer ;
var x, y  : real ;
var arr   : array [ 1 .. 10 ] of integer ;

function add ( a, b : integer ) : integer ;
begin
    add := a + b
end ;

procedure swap ( a, b : integer ) ;
var temp : integer ;
begin
    temp := a ;
    a    := b ;
    b    := temp
end ;

begin
    { test all relops }
    if i = 0  then j := 1 else j := 2 ;
    if i <> 0 then j := 3 else j := 4 ;
    if i <  5 then j := 4 else j := 5 ;
    if i <= 5 then j := 5 else j := 6 ;
    if i >= 5 then j := 6 else j := 7 ;
    if i >  5 then j := 7 else j := 8 ;

    { test numeric literals }
    x := 3.14 ;
    y := 2.5e10 ;
    y := 1.0E-3 ;
    i := 42 ;

    { test arithmetic }
    i := i + j - 1 ;
    i := i * 2 ;
    i := i div 3 ;
    i := i mod 7 ;
    x := x / y ;

    { test logical }
    if (i > 0) and (j < 10) then j := 0 else j := 1 ;
    if (i = 0) or  (j = 0)  then j := 1 else j := 2 ;
    if not (i = j) then j := 2 else j := 3 ;

    { test array }
    arr[1] := add(i, j) ;

    { test while }
    while i > 0 do
        i := i - 1

end.
