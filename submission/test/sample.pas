{ Sample Pascal program for smoke-testing all three parsers }
program sample ( input, output ) ;

var x, y : integer ;
var z    : real ;

begin
    x := 10 ;
    y := x + 2 ;
    if x < y then
        z := 3.14
    else
        z := 0.0
end.
