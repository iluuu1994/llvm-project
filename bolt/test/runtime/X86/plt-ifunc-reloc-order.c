// REQUIRES: x86_64-linux, gnu_ld
//
// RUN: split-file %s %t
// RUN: %clang %cflags -O2 -g -fPIC -shared -o %t/libexample.so \
// RUN:   %t/example.c
// RUN: %clang %cflags -O2 -g -fno-pie -no-pie -fuse-ld=bfd \
// RUN:   -Wl,--emit-relocs -Wl,-rpath,\$ORIGIN -o %t/repro %t/main.c \
// RUN:   -L%t -lexample
// RUN: llvm-bolt %t/repro -o %t/repro.bolt
// RUN: llvm-readelf --dyn-relocations %t/repro.bolt | FileCheck %s
// RUN: %t/repro.bolt

// CHECK-LABEL: 'PLT' relocation section
// CHECK:      R_X86_64_JUMP_SLOT{{.*}}long_name
// CHECK-NEXT: R_X86_64_IRELATIVE

//--- main.c
extern int long_name(void);

__attribute__((target_clones("default,avx2"))) int foo(int x) { return x + 1; }

int main(void) {
  if (foo(1) != 2)
    return 1;
  if (long_name() != 0)
    return 1;
  return 0;
}

//--- example.c
int long_name(void) { return 0; }
