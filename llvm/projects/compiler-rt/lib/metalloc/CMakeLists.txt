set(METALLOC_SOURCES
  metastack.cc
  metaglobal.cc
  metautils.cc
  )

include_directories(..)

set(METALLOC_CFLAGS ${SANITIZER_COMMON_CFLAGS})

add_custom_target(metalloc)

add_compiler_rt_runtime(clang_rt.metalloc
        STATIC 
        ARCHS x86_64 
        SOURCES ${METALLOC_SOURCES}
              $<TARGET_OBJECTS:RTInterception.x86_64>
        CFLAGS ${METALLOC_CFLAGS}
 
        PARENT_TARGET metalloc)

add_sanitizer_rt_symbols(clang_rt.metalloc)

add_dependencies(compiler-rt metalloc)
