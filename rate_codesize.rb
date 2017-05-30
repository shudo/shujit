#!/usr/bin/ruby
#
# calculate the ratio:
#   size of generated native code/
#    {size of byte code, number of byte code instructions}
#
# 1. Set JAVA_COMPILER_OPT codesize
# 2. You'll get the file `jit_codesize' running Java program with JIT compiler.
# 3. % rate_codesize.rb jit_codesize

filename = 'jit_codesize'

nmethod = 0
ninst = 0
byte_size = 0
native_size = 0
tentry_size = 0

if $*[0]
  filename = $*[0]
end

#print "#{filename}\n"

begin
  f = open(filename, "r")
  $stdin = f
end

while gets()
  next if /^#/
  chomp!
  elems = split

  nmethod += 1
  ninst += elems[0].to_i
  byte_size += elems[1].to_i
  native_size += elems[2].to_i
  tentry_size += elems[4].to_i
end

if f then
  f.close
end

print "num compiled method: #{nmethod}\n"
print "num bytecode:        #{ninst}\n"
print "size bytecode:       #{byte_size}\n"
print "size native code:    #{native_size}\n"

print "size native code / num bytecode:  #{native_size.to_f / ninst}\n"
print "size native code / size bytecode: #{native_size.to_f / byte_size}\n"

print "size throw entry:    #{tentry_size}\n"

print "all generated:       #{native_size + tentry_size}\n"

print "all generated / num bytecode:     #{(native_size + tentry_size).to_f / ninst}\n"
print "all generated / size bytecode:    #{(native_size + tentry_size).to_f / byte_size}\n"
