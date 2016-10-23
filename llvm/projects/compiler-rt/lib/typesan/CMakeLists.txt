set(TYPESAN_SOURCES
  typesan.cc
  )

include_directories(..)

set(TYPESAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})

add_custom_target(typesan)

add_compiler_rt_runtime(clang_rt.typesan
        STATIC 
        ARCHS x86_64 
        SOURCES ${TYPESAN_SOURCES}
        CFLAGS ${TYPESAN_CFLAGS}
 
        PARENT_TARGET typesan)

add_sanitizer_rt_symbols(clang_rt.typesan)

add_dependencies(compiler-rt typesan)
