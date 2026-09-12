# пример PureL
print "hello from file"
let a = 2
let b = 3
print a, "+", b, "=", a + b

fun fact(n)
  if n <= 1 then
    return 1
  end
  return n * fact(n - 1)
end

print "5! =", fact(5)
