#!/bin/sh
# Recreate the persistent Ghidra project from the relocated ELF and apply PS4 import names.
export JAVA_HOME=$HOME/tools/jdk-21.0.13+9 PATH=$HOME/tools/jdk-21.0.13+9/bin:$PATH
exec $HOME/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless $HOME/kr-port/ghidra KRBattrideWar \
  -import $HOME/kr-port/eboot_v102_ghidra_reloc.elf -loader ElfLoader -loader-imagebase 0x100000 \
  -scriptPath $HOME/kr-port/ghidra/scripts -postScript ApplyPs4Names.java $HOME/kr-port/plt_v102.json \
  -max-cpu 6
