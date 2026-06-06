{ Edge-case lexer tests }
program edges ( output ) ;
var
    a : array [ 10 .. 20 ] of real ;   { dotdot inside array bounds }
    b : array [ 1..5 ] of integer ;    { dotdot with no spaces }
begin
    a[10] := 1.0E+2 ;    { real with positive exponent }
    a[11] := 9.9E-1 ;    { real with negative exponent }
    b[1]  := 100 div 3 ;
    b[2]  := 100 mod 7 ;
    @ { unknown char — should get UNKNOWN token + error }
end.
