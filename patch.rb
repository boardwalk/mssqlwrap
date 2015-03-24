#!/usr/bin/env ruby

file = ARGV.shift

contents = File.open(file, "rb") { |f| f.read }

needle = '/opt/microsoft/sqlncli/%d.%d.%d.%d/en_US/'
repl   = '/home/user/dskorupski/mssql/%d.%d.%d.%d/'
repl = repl + "\0" * (needle.size - repl.size)
contents = contents.gsub(needle, repl)

File.open(file, "wb") { |f| f.write(contents) }
