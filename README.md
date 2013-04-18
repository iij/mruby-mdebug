# mruby-mdebug

**(pre-alpha release)**

mruby-mdebug helps you to debug mruby script and mruby interpreter.

## How to use
0. install GNU readline library
1. add 2 lines to your build\_config.rb
   ```Ruby

      conf.cc.defines << %w(ENABLE_DEBUG)
      conf.gem :github => "iij/mruby-mdebug"
   ```
2. build mruby
3. run ``bin/mrdb``

## Example
```text
% bin/mrdb c1.rb
mruby debugger

c1.rb:23:end
(mrdb)l
=> l
[18, 25] in c1.rb
   18    end
   19  
   20    def z
   21      true
   22    end
=> 23  end
   24  p A.new.a
(mrdb)vm
=> vm
Callinfo:
  ci[0]: nregs=4, ridx=0, eidx=0, stackidx=0, method=(null), irep 469
Stack (base) [* => mrb->stack]:
  004  (MODULE:0x208e93b10)
  003  (CLASS:0x208e942c0)
  002  false
  001  false
 *000  main
Rescue list:
  (none)
Ensure list:
  (none)
Environment:
  (none)
(mrdb)l 9
=> l 9
[4, 14] in c1.rb
    4      begin
    5        b
    6      rescue TypeError
    7        r = self.z
    8      end
    9      [ r, @e ]
   10    end
   11  
   12    def b
   13      begin
(mrdb)b 7
=> b 7
(mrdb)c
=> c
c1.rb:7:      r = self.z
(mrdb)l
=> l
[2, 12] in c1.rb
    2    def a
    3      r = @e = false
    4      begin
    5        b
    6      rescue TypeError
=>  7        r = self.z
    8      end
    9      [ r, @e ]
   10    end
   11  
(mrdb)vm
=> vm
Callinfo:
  ci[1]: nregs=5, ridx=0, eidx=0, stackidx=0, method=a, irep 471
  ci[0]: nregs=4, ridx=0, eidx=0, stackidx=0, method=(null), irep 469
Stack (base) [* => mrb->stack]:
  005  (OBJECT:0x208e937b0)
  004  false
  003  false
 *002  (OBJECT:0x208e938a0)
  001  main
  000  main
Rescue list:
  (none)
Ensure list:
  (none)
Environment:
  (none)
(mrdb)
Bye.

```

## Available Commands
```text
MRuby Debugger help
  b[reak]      set a breakpoint
  c[ont]       continue
  h[elp]       show command reference
  l[ist]       show source code
  n[ext]       step program
  vm           show internal structures of VM
```
