define i4 @add(i4 %a, i4 %b) {
  %c = add i4 %a, %b
  ret i4 %c
}

define i4 @sub(i4 %a, i4 %b) {
  %c = sub i4 %a, %b
  ret i4 %c
}

define i4 @and(i4 %a, i4 %b) {
  %c = and i4 %a, %b
  ret i4 %c
}

define i4 @or(i4 %a, i4 %b) {
  %c = or i4 %a, %b
  ret i4 %c
}

define i4 @xor(i4 %a, i4 %b) {
  %c = xor i4 %a, %b
  ret i4 %c
}

define i4 @not(i4 %a) {
  %c = xor i4 %a, -1
  ret i4 %c
}

define i2 @trunc(i4 %a) {
  %c = trunc i4 %a to i2
  ret i2 %c
}

define i8 @zext(i4 %a) {
  %c = zext i4 %a to i8
  ret i8 %c
}

define i8 @sext(i4 %a) {
  %c = sext i4 %a to i8
  ret i8 %c
}
