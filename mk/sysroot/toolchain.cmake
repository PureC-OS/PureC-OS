set(CMAKE_SYSTEM_NAME PureC)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_CROSSCOMPILING TRUE)

get_filename_component(PUREC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(DEFINED ENV{BIN_DIR})
  set(PUREC_BIN "$ENV{BIN_DIR}")
else()
  set(PUREC_BIN "${PUREC_ROOT}/bin")
endif()
set(PUREC_SYSROOT "${PUREC_BIN}/sysroot" CACHE PATH "PureC dependency prefix")
set(PUREC_CXX_STAGE "${PUREC_BIN}/cxx-stage" CACHE PATH "libc++ stage")
set(PUREC_NS_STAGE "${PUREC_BIN}/ns-stage" CACHE PATH "zlib, libpng, libjpeg, expat stage")
set(PUREC_LIBC_INCLUDE "${PUREC_ROOT}/src/libc/include/hosted")
set(PUREC_LINKER_SCRIPT "${PUREC_ROOT}/linker-userspace-cxx.ld")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_program(PUREC_CLANG clang REQUIRED)
find_program(PUREC_CLANGXX clang++ REQUIRED)
find_program(PUREC_LD x86_64-elf-ld REQUIRED)
find_program(PUREC_AR llvm-ar REQUIRED)
find_program(PUREC_RANLIB llvm-ranlib REQUIRED)
execute_process(COMMAND "${PUREC_CLANG}" --print-resource-dir
                OUTPUT_VARIABLE PUREC_CLANG_RESOURCE OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND x86_64-elf-gcc -print-libgcc-file-name
                OUTPUT_VARIABLE PUREC_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_C_COMPILER "${PUREC_CLANG}")
set(CMAKE_CXX_COMPILER "${PUREC_CLANGXX}")
set(CMAKE_ASM_COMPILER "${PUREC_CLANG}")
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-elf)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-elf)
set(CMAKE_ASM_COMPILER_TARGET x86_64-unknown-elf)
set(CMAKE_LINKER "${PUREC_LD}")
set(CMAKE_AR "${PUREC_AR}")
set(CMAKE_RANLIB "${PUREC_RANLIB}")

set(PUREC_COMMON_FLAGS
    "-D__purec__=1 -m64 -mno-red-zone -fno-stack-protector -fno-pic -fno-pie -mcmodel=small -nostdinc")
set(CMAKE_C_FLAGS_INIT
    "${PUREC_COMMON_FLAGS} -isystem ${PUREC_CLANG_RESOURCE}/include -isystem ${PUREC_SYSROOT}/include -isystem ${PUREC_NS_STAGE}/include -isystem ${PUREC_LIBC_INCLUDE}")
set(CMAKE_CXX_FLAGS_INIT
    "${PUREC_COMMON_FLAGS} -nostdinc++ -fexceptions -frtti -isystem ${PUREC_CXX_STAGE}/include/c++/v1 -isystem ${PUREC_CLANG_RESOURCE}/include -isystem ${PUREC_SYSROOT}/include -isystem ${PUREC_NS_STAGE}/include -isystem ${PUREC_LIBC_INCLUDE}")
set(CMAKE_ASM_FLAGS_INIT "--target=x86_64-unknown-elf -m64")

set(CMAKE_FIND_ROOT_PATH "${PUREC_SYSROOT}" "${PUREC_NS_STAGE}" "${PUREC_CXX_STAGE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_PREFIX_PATH "${PUREC_SYSROOT}" "${PUREC_NS_STAGE}")
set(CMAKE_LIBRARY_PATH "${PUREC_SYSROOT}/lib" "${PUREC_NS_STAGE}/lib" "${PUREC_BIN}/lib")
set(CMAKE_INCLUDE_PATH "${PUREC_SYSROOT}/include" "${PUREC_NS_STAGE}/include")

set(ENV{PKG_CONFIG_LIBDIR} "${PUREC_SYSROOT}/lib/pkgconfig:${PUREC_NS_STAGE}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

file(MAKE_DIRECTORY "${PUREC_SYSROOT}/lib")
foreach(stub m pthread dl rt c util atomic)
  if(NOT EXISTS "${PUREC_SYSROOT}/lib/lib${stub}.a")
    execute_process(COMMAND "${PUREC_AR}" rc "${PUREC_SYSROOT}/lib/lib${stub}.a")
  endif()
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${PUREC_SYSROOT}/lib -L${PUREC_BIN}/lib")
