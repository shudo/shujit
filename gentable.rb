#!/usr/local/bin/ruby

# constants
CONST_C_FNAME = 'constants.c'
CONST_H_FNAME = 'constants.h'

NOPCODES = 336
NSTATES = 5
STANY = 5
STSTA = 5


def outTableToH(f, var_name, array_size, max_elem)
  if (max_elem < 256)
    type = 'unsigned char'
  elsif (max_elem < 65536)
    type = 'unsigned short'
  else
    type = 'unsigned int'
  end

  f.print "extern #{type} #{var_name}[][#{NSTATES}][#{array_size + 1}];\n"
end

def outTableToC(f, var_name, table, array_size, max_elem)
  if (max_elem < 256)
    type = 'unsigned char'
  elsif (max_elem < 65536)
    type = 'unsigned short'
  else
    type = 'unsigned int'
  end

  f.print "/* #{var_name}[opcode][state][] */\n"
  f.print "#{type} #{var_name}[][#{NSTATES}][#{array_size + 1}] = {\n"
  (0..(NOPCODES - 1)).each do |i|
    f.print "  /* #{i} */ {"
    (0..(NSTATES - 1)).each do |j|
      array = table[i][j]
      f.print "{"
      array.each {|off| f.print "#{off}," } if array
      f.print "0}"
      if j == NSTATES - 1
	f.print ""
      else
	f.print ","
      end
    end
    f.print "}"
    if i == NOPCODES - 1
      f.print "\n"
    else
      f.print ",\n"
    end
  end
  f.print "};\n\n"
end


# search beginning of the function assembledCode()
start_addr = -1

while gets()
  next if $_ !~ /assembledCode>/

  chomp!()
  elems = split()

  gets()
  elems = split()
  $_ = elems[0]
  chop!()
  start_addr = $_.hex()
#print "start_addr is #{start_addr}\n"
  break
end


# skip invalid codes
if PLATFORM !~ /-cygwin/
  while gets()
    break if /_GLOBAL_OFFSET_TABLE_/
  end
end


code_table = Array.new(NOPCODES)
func_table = Array.new(NOPCODES)
constant_table = Array.new(NOPCODES)
bytepc_table = Array.new(NOPCODES)
jumpexc_table = Array.new(NOPCODES)
#jump_table = Array.new(NOPCODES)
jumpret_table = Array.new(NOPCODES)

(0..(NOPCODES - 1)).each do |i|
  code_table[i] = Array.new(NSTATES)
  func_table[i] = Array.new(NSTATES)
  constant_table[i] = Array.new(NSTATES)
  bytepc_table[i] = Array.new(NSTATES)
  jumpexc_table[i] = Array.new(NSTATES)
#  jump_table[i] = Array.new(NSTATES)
  jumpret_table[i] = Array.new(NSTATES)
end


opcode = -1
code_addr = start_addr
code_offset = -1
code_len = -1
init_state = -1
last_state = -1

max_code_offset = -1
max_code_len = -1
max_constant_off = -1;
max_func_off = -1;
max_bytepc_off = -1;
max_jumpexc_off = -1;

func_arysize = 0
constant_arysize = 0
bytepc_arysize = 0
jumpexc_arysize = 0
jump_arysize = 0
jumpret_arysize = 0


strip_underscore = nil
if PLATFORM =~ /-freebsd2/ || PLATFORM =~ /-cygwin/
  strip_underscore = true
end


while gets()
  chomp!()
  elems = split()

  if elems[2] == '(bad)'	# code header
#print "(bad) is found\n"
    gets()
    chomp!()
    elems = split()

    $_ = elems[0]
    chop!()
    code_addr_new = $_.hex() + 5
    if opcode != -1
      code_len = code_addr_new - code_addr - 6
      max_code_len = code_len if code_len > max_code_len
      if init_state == STANY
	(0..(NSTATES - 1)).each do |i|
	  code_table[opcode][i] = [code_offset, code_len, last_state]
	end
      else
	code_table[opcode][init_state] = [code_offset, code_len, last_state]
      end
    end
    code_addr = code_addr_new
    code_offset = code_addr - start_addr
    max_code_offset = code_offset if code_offset > max_code_offset

    break if elems[2] == '(bad)'

    opcode = elems[2].hex() + (elems[3].hex() << 8)
    init_state = elems[4][0,1].hex()
    last_state = elems[4][1,1].hex()

#print "addr: #{code_addr}, opcode: #{opcode}, init_state: #{init_state}, last_state: #{last_state}\n"

  elsif /(0x606060|e9 60 60 60 00|e8 60 60 60 00)/	# constant
	# must match with
	#   `e9 60 60 60 00' (jmp), `e8 60 60 60 00' (call)
	#   and `... 60 60 60<NL>00    ... 0x606060 ...'.
    i = 1
    i += 1 while elems[i] != '60'
    off = elems[0].chop().hex() + i - 1 - code_addr
    max_constant_off = off if off > max_constant_off
#print "constant offset: #{off}\n"
    if init_state == STANY
      array = constant_table[opcode][0]
    else
      array = constant_table[opcode][init_state]
    end
    if (array)
      array << off
    else
      array = [off]
    end
    arysize = array.length()
    constant_arysize = arysize if arysize > constant_arysize
    if init_state == STANY
      (0..(NSTATES - 1)).each {|i| constant_table[opcode][i] = array }
    else
      constant_table[opcode][init_state] = array
    end

  elsif /[ \t]call[ \t]/		# function call
    if /[ \t]call[ \t]*\*/	# cancel `call *%register'
      next
    end

    off = elems[0].chop().hex() + 1 - code_addr

    gets()
    chomp!()
    elems = split()
    funcname = elems.pop()
    if funcname =~ /^%/ || funcname =~ /^0x/ || funcname == '*ABS*'
	# %register or `*ABS*'
      next
    end
    max_func_off = off if off > max_func_off

    # _funcname -> funcname
    if funcname =~ /^_/ && strip_underscore
      # in the case of Win32 and FreeBSD 2.X
      funcname = funcname[1...funcname.length()]
    end
#print "function offset: #{off}, name: #{funcname}\n"
    if init_state == STANY
      array = func_table[opcode][0]
    else
      array = func_table[opcode][init_state]
    end
    if (array)
      array << [off, funcname]
    else
      array = [[off, funcname]]
    end
    arysize = array.length()
    func_arysize = arysize if arysize > func_arysize
    if init_state == STANY
      (0..(NSTATES - 1)).each {|i| func_table[opcode][i] = array }
    else
      func_table[opcode][init_state] = array
    end

  elsif /0x626262/		# bytepcoff
    i = 1
    i += 1 while elems[i] != '62'
    off = elems[0].chop().hex() + i - 1 - code_addr
#print "bytepcoff offset: #{off}\n"
    max_bytepc_off = off if off > max_bytepc_off
    if init_state == STANY
      array = bytepc_table[opcode][0]
    else
      array = bytepc_table[opcode][init_state]
    end
    if (array)
      array << off
    else
      array = [off]
    end
    arysize = array.length()
    bytepc_arysize = arysize if arysize > bytepc_arysize
    if init_state == STANY
      (0..(NSTATES - 1)).each {|i| bytepc_table[opcode][i] = array }
    else
      bytepc_table[opcode][init_state] = array
    end

  elsif /70 70/		# jump to exception handler
    i = 1
    i += 1 while elems[i] != '70'
    off = elems[0].chop().hex() + i - 2 - code_addr
    max_jumpexc_off = off if off > max_jumpexc_off
#print "jump to exception handler: #{off}\n"
    if init_state == STANY
      array = jumpexc_table[opcode][0];
    else
      array = jumpexc_table[opcode][init_state];
    end
    if (array)
      array << off
    else
      array = [off]
    end
    arysize = array.length()
    jumpexc_arysize = arysize if arysize > jumpexc_arysize
    if init_state == STANY
      (0..(NSTATES - 1)).each {|i| jumpexc_table[opcode][i] = array }
    else
      jumpexc_table[opcode][init_state] = array
    end

#  elsif /72 72/		# jump instruction
#    i = 1
#    i += 1 while elems[i] != '72'
#     off = elems[0].chop().hex() + i - 2 - code_addr
# #print "jump instruction: #{off}\n"
#     if init_state == STANY
#       array = jump_table[opcode][0];
#     else
#       array = jump_table[opcode][init_state];
#     end
#     if (array)
#       array << off
#     else
#       array = [off]
#     end
#     arysize = array.length()
#     jump_arysize = arysize if arysize > jump_arysize
#     if init_state == STANY
#       (0..(NSTATES - 1)).each {|i| jump_table[opcode][i] = array }
#     else
#       jump_table[opcode][init_state] = array
#     end

#   elsif /74 74/		# return instruction
#     i = 1
#     i += 1 while elems[i] != '74'
#     off = elems[0].chop().hex() + i - 2 - code_addr
# #print "return instruction: #{off}\n"
#     if init_state == STANY
#       array = jumpret_table[opcode][0];
#     else
#       array = jumpret_table[opcode][init_state];
#     end
#     if (array)
#       array << off
#     else
#       array = [off]
#     end
#     arysize = array.length()
#     jumpret_arysize = arysize if arysize > jumpret_arysize
#     if init_state == STANY
#       (0..(NSTATES - 1)).each {|i| jumpret_table[opcode][i] = array }
#     else
#       jumpret_table[opcode][init_state] = array
#     end
  end
end


if max_code_len > 255
  print "FATAL: max code length exceeds 255 !!!\n"
	# throwentry (compiler.h) assumes the length of native code <= 255
  exit 1
end


open(CONST_C_FNAME, "w") do |f|
  f.print "\#include \"#{CONST_H_FNAME}\"\n\n"

  # CodeTable code_table[NOPCODES][NSTATES]
  f.print "/* code_table[opcode][state] */\n"
  f.print "CodeTable code_table[][#{NSTATES}] = {\n"
  (0..(NOPCODES - 1)).each do |i|
    f.print "  {\t/* #{i} */\n"
    (0..(NSTATES - 1)).each do |j|
      $_ = code_table[i][j]
      if $_
	f.print "    {#{$_[0]}, #{$_[1]}, #{$_[2]}}"
      else
	f.print "    {0, 0, 0}"
      end
      if j == NSTATES - 1
	f.print "\n"
      else
	f.print ",\n"
      end
    end
    f.print "  }"
    if i == NOPCODES - 1
      f.print "\n"
    else
      f.print ",\n"
    end
  end
  f.print "};\n\n"

  # FuncTable func_table[NOPCODES][NSTATES]
  f.print "/* func_table[opcode][state][] */\n"
  f.print "FuncTable func_table[][#{NSTATES}][#{func_arysize + 1}] = {\n"
  (0..(NOPCODES - 1)).each do |i|
    f.print "  {\t/* #{i} */\n"
    (0..(NSTATES - 1)).each do |j|
      f.print "    {"
      $_ = func_table[i][j]
      $_.each {|array| f.print "{#{array[0]}, (unsigned char *)#{array[1]}}, "} if $_
      f.print "{-1, 0}"
      if j == NSTATES - 1
	f.print "}\n"
      else
	f.print "},\n"
      end
    end
    f.print "  }"
    if i == NOPCODES - 1
      f.print "\n"
    else
      f.print ",\n"
    end
  end
  f.print "};\n\n"

  # constant_table[][NSTATE][]
  outTableToC(f, 'constant_table', constant_table, constant_arysize, max_constant_off)
  # bytepc_table[][NSTATE][]
  outTableToC(f, 'bytepc_table', bytepc_table, bytepc_arysize, max_bytepc_off)
  # jumpexc_table[][NSTATE][]
  outTableToC(f, 'jumpexc_table', jumpexc_table, jumpexc_arysize, max_jumpexc_off)
#  # jump_table[][NSTATE][]
#  outTableToC(f, 'jump_table', jump_table, jump_arysize, 255)
#  # jumpret_table[][NSTATE][]
#  outTableToC(f, 'jumpret_table', jumpret_table, jumpret_arysize, 255)
end


open(CONST_H_FNAME, "w") do |f|
  f.print <<EOF
#include "../compiler.h"

#ifdef WIN32
extern _int64 _alldiv(_int64 x, _int64 y);
extern _int64 _allrem(_int64 x, _int64 y);
#else
extern long long int __divdi3(long long int x, long long int y);
extern long long int __moddi3(long long int x, long long int y);
#endif
extern double fmod(double x, double y);

#if JDK_VER >= 12
extern sys_mon_t * monitorEnter2(struct execenv *, uintptr_t);
extern int monitorExit2(struct execenv *, uintptr_t);

extern double jsin(double);
extern double jcos(double);
extern double jtan(double);
#endif
extern double sin(double);
extern double cos(double);
extern double tan(double);

EOF
  if max_code_offset < 65536 && max_code_len < 256
    f.print <<EOF
typedef struct codetable {
  unsigned short offset;
  unsigned char length;
  char last_state;
} CodeTable;
EOF
  else
    f.print <<EOF
typedef struct codetable {
  unsigned int offset;
  unsigned short length;
  short last_state;
} CodeTable;
EOF
  end
  f.print "\n"

  f.print "extern CodeTable code_table[][#{NSTATES}];\n\n"

  f.print "typedef struct functable {\n  "
  if max_func_off < 128
    f.print "char"
  elsif max_func_off < 32768
    f.print "short"
  else
    f.print "int"
  end
  f.print <<EOF
 offset;
  unsigned char *address;
} FuncTable;

EOF

  f.print "extern FuncTable func_table[][#{NSTATES}][#{func_arysize + 1}];\n\n"

  outTableToH(f, 'constant_table', constant_arysize, max_constant_off)
  outTableToH(f, 'bytepc_table', bytepc_arysize, max_bytepc_off)
  outTableToH(f, 'jumpexc_table', jumpexc_arysize, max_jumpexc_off)
#  outTableToH(f, 'jump_table', jump_arysize, 255)
#  outTableToH(f, 'jumpret_table', jumpret_arysize, 255)
end
