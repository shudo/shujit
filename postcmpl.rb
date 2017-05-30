#!/usr/local/bin/ruby

while gets()
  next  if /PUSH/
  # translate "leal LC...,%esi" to "pushl LC..."  if next line is `#APP'
  #	Linux: .LC..., FreeBSD: LC...
  if /leal \.?LC/
    line = $_;
    gets()
    if /^#APP/
      line.gsub!(/leal (\.?LC.*),.*/, 'pushl \1')
    end
    print line
  end
  # skip PUSH ...
  print
end
