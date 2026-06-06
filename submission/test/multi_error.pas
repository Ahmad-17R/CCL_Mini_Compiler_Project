{ Multi-error test — several deliberate mistakes }
program multierr ( output ) ;

var x : integer ;
var y : real ;

begin
    x := ;          { error 1: missing expression after := }
    y := 3.14 ;
    z := 1          { error 2: undeclared identifier z }
end.
