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

define i2 @mul(i2 %a, i2 %b) {
  %c = mul i2 %a, %b
  ret i2 %c
}

define {i4, i1} @uadd_with_overflow(i4 %a, i4 %b) {
  %c = call {i4, i1} @llvm.uadd.with.overflow.i4(i4 %a, i4 %b)
  ret {i4, i1} %c
}

define {i4, i1} @usub_with_overflow(i4 %a, i4 %b) {
  %c = call {i4, i1} @llvm.usub.with.overflow.i4(i4 %a, i4 %b)
  ret {i4, i1} %c
}

define {i4, i1} @sadd_with_overflow(i4 %a, i4 %b) {
  %c = call {i4, i1} @llvm.sadd.with.overflow.i4(i4 %a, i4 %b)
  ret {i4, i1} %c
}

define {i4, i1} @ssub_with_overflow(i4 %a, i4 %b) {
  %c = call {i4, i1} @llvm.ssub.with.overflow.i4(i4 %a, i4 %b)
  ret {i4, i1} %c
}

define i4 @ctpop(i4 %a) {
  %c = call i4 @llvm.ctpop.i4(i4 %a)
  ret i4 %c
}

define i4 @select(i1 %c, i4 %a, i4 %b) {
  %d = select i1 %c, i4 %a, i4 %b
  ret i4 %d
}

define i1 @icmp_eq(i4 %a, i4 %b) {
  %c = icmp eq i4 %a, %b
  ret i1 %c
}

define i1 @icmp_ne(i4 %a, i4 %b) {
  %c = icmp ne i4 %a, %b
  ret i1 %c
}

define i1 @icmp_ult(i4 %a, i4 %b) {
  %c = icmp ult i4 %a, %b
  ret i1 %c
}

define i1 @icmp_ule(i4 %a, i4 %b) {
  %c = icmp ule i4 %a, %b
  ret i1 %c
}

define i1 @icmp_ugt(i4 %a, i4 %b) {
  %c = icmp ugt i4 %a, %b
  ret i1 %c
}

define i1 @icmp_uge(i4 %a, i4 %b) {
  %c = icmp uge i4 %a, %b
  ret i1 %c
}

define i1 @icmp_slt(i4 %a, i4 %b) {
  %c = icmp slt i4 %a, %b
  ret i1 %c
}

define i1 @icmp_sle(i4 %a, i4 %b) {
  %c = icmp sle i4 %a, %b
  ret i1 %c
}

define i1 @icmp_sgt(i4 %a, i4 %b) {
  %c = icmp sgt i4 %a, %b
  ret i1 %c
}

define i1 @icmp_sge(i4 %a, i4 %b) {
  %c = icmp sge i4 %a, %b
  ret i1 %c
}

define i4 @shl(i4 %a, i4 %b) {
  %c = shl i4 %a, %b
  ret i4 %c
}

define i4 @lshr(i4 %a, i4 %b) {
  %c = lshr i4 %a, %b
  ret i4 %c
}

define i4 @ashr(i4 %a, i4 %b) {
  %c = ashr i4 %a, %b
  ret i4 %c
}

define i2 @add_chain(i2 %a, i2 %b, i2 %c) {
  %x = add i2 %a, %b
  %y = add i2 %x, %c
  ret i2 %y
}

define i2 @add_reuse(i2 %a, i2 %b, i2 %c) {
  %x = add i2 %a, %b
  %y = add i2 %a, %c
  %z = add i2 %x, %y
  ret i2 %z
}

define i4 @bitreverse(i4 %a) {
  %c = call i4 @llvm.bitreverse.i4(i4 %a)
  ret i4 %c
}

define i4 @abs_poison(i4 %a) {
  %c = call i4 @llvm.abs.i4(i4 %a, i1 true)
  ret i4 %c
}

define i4 @abs_nopoison(i4 %a) {
  %c = call i4 @llvm.abs.i4(i4 %a, i1 false)
  ret i4 %c
}

define i4 @fshl(i4 %a, i4 %b, i4 %c) {
  %d = call i4 @llvm.fshl.i4(i4 %a, i4 %b, i4 %c)
  ret i4 %d
}

define i4 @fshr(i4 %a, i4 %b, i4 %c) {
  %d = call i4 @llvm.fshr.i4(i4 %a, i4 %b, i4 %c)
  ret i4 %d
}

define i4 @udiv(i4 %a, i4 %b) {
  %c = udiv i4 %a, %b
  ret i4 %c
}

define i4 @urem(i4 %a, i4 %b) {
  %c = urem i4 %a, %b
  ret i4 %c
}

define i4 @sdiv(i4 %a, i4 %b) {
  %c = sdiv i4 %a, %b
  ret i4 %c
}

define i4 @srem(i4 %a, i4 %b) {
  %c = srem i4 %a, %b
  ret i4 %c
}

define i4 @smin(i4 %a, i4 %b) {
  %c = call i4 @llvm.smin.i4(i4 %a, i4 %b)
  ret i4 %c
}

define i4 @smax(i4 %a, i4 %b) {
  %c = call i4 @llvm.smax.i4(i4 %a, i4 %b)
  ret i4 %c
}

define i4 @umin(i4 %a, i4 %b) {
  %c = call i4 @llvm.umin.i4(i4 %a, i4 %b)
  ret i4 %c
}

define i4 @umax(i4 %a, i4 %b) {
  %c = call i4 @llvm.umax.i4(i4 %a, i4 %b)
  ret i4 %c
}

declare {i4, i1} @llvm.uadd.with.overflow.i4(i4 %a, i4 %b)
declare {i4, i1} @llvm.usub.with.overflow.i4(i4 %a, i4 %b)
declare {i4, i1} @llvm.sadd.with.overflow.i4(i4 %a, i4 %b)
declare {i4, i1} @llvm.ssub.with.overflow.i4(i4 %a, i4 %b)
declare i4 @llvm.ctpop.i4(i4 %a)
declare i4 @llvm.bitreverse.i4(i4 %a)
declare i4 @llvm.abs.i4(i4 %a, i1 %is_int_min_poison)
declare i4 @llvm.fshl.i4(i4 %a, i4 %b, i4 %c)
declare i4 @llvm.fshr.i4(i4 %a, i4 %b, i4 %c)
declare i4 @llvm.smin.i4(i4 %a, i4 %b)
declare i4 @llvm.smax.i4(i4 %a, i4 %b)
declare i4 @llvm.umin.i4(i4 %a, i4 %b)
declare i4 @llvm.umax.i4(i4 %a, i4 %b)
