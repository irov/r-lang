(types
  (type 1 bool)
  (type 2 i8)
  (type 3 i16)
  (type 4 i32)
  (type 5 i64)
  (type 6 isize)
  (type 7 u8)
  (type 8 u16)
  (type 9 u32)
  (type 10 u64)
  (type 11 usize)
  (type 12 f32)
  (type 13 f64)
  (type 14 char)
  (type 15 str)
  (type 16 void)
  (type 17 never)
  (type 18 (atomic i8))
  (type 19 (atomic i16))
  (type 20 (atomic i32))
  (type 21 (atomic i64))
  (type 22 (atomic isize))
  (type 23 (atomic u8))
  (type 24 (atomic u16))
  (type 25 (atomic u32))
  (type 26 (atomic u64))
  (type 27 (atomic usize))
  (type 28 c_char)
  (type 29 c_schar)
  (type 30 c_uchar)
  (type 31 c_short)
  (type 32 c_ushort)
  (type 33 c_int)
  (type 34 c_uint)
  (type 35 c_long)
  (type 36 c_ulong)
  (type 37 c_llong)
  (type 38 c_ullong)
  (type 39 c_bool)
  (type 40 c_wchar)
  (type 41 c_wint)
  (type 42 c_int8)
  (type 43 c_uint8)
  (type 44 c_int16)
  (type 45 c_uint16)
  (type 46 c_int32)
  (type 47 c_uint32)
  (type 48 c_int64)
  (type 49 c_uint64)
  (type 50 c_intptr)
  (type 51 c_uintptr)
  (type 52 c_intmax)
  (type 53 c_uintmax)
  (type 54 c_float)
  (type 55 c_double)
  (type 56 c_long_double)
  (type 57 c_size)
  (type 58 c_ptrdiff)
  (type 59 (struct "test.codegen.value_types"::"TestAssertionFailed"))
  (type 60 (struct "test.codegen.value_types"::"character_literal_set"))
  (type 61 (struct "test.codegen.value_types"::"issue"))
  (type 62 (struct "test.codegen.value_types"::"scalar_defaults"))
  (type 63 (struct "test.codegen.value_types"::"scalar_integers"))
  (type 64 (struct "test.codegen.value_types"::"scalar_leaf"))
  (type 65 (enum "test.codegen.value_types"::"scalar_state"))
  (type 66 (struct "test.codegen.value_types"::"scalar_tree"))
  (type 67 (fixed_array u16 2))
  (type 68 (option i64))
  (type 69 (option u16))
  (type 70 (option i32))
  (type 71 (effects (struct "test.codegen.value_types"::"issue")))
  (type 72 (carrier i32 (effects (struct "test.codegen.value_types"::"issue"))))
  (type 73 (carrier void (effects (struct "test.codegen.value_types"::"issue"))))
  (type 74 (const_slice i32))
  (type 75 (standard "core::utf8_error"))
  (type 76 (standard "std.alloc::alloc_error"))
  (type 77 (standard "std.async::start_error"))
  (type 78 (standard "std.bits::read_error"))
  (type 79 (standard "std.bytes::bytes_error"))
  (type 80 (standard "std.c::runtime_error"))
  (type 81 (standard "std.c::string_error"))
  (type 82 (standard "std.convert::parse_error"))
  (type 83 (standard "std.convert::range_error"))
  (type 84 (standard "std.env::env_error"))
  (type 85 (standard "std.error::error"))
  (type 86 (standard "std.format::format_error"))
  (type 87 (standard "std.fs::fs_error"))
  (type 88 (standard "std.fs::path_error"))
  (type 89 (standard "std.io::io_error"))
  (type 90 (standard "std.math::math_error"))
  (type 91 (standard "std.net::address_error"))
  (type 92 (standard "std.net::net_error"))
  (type 93 (standard "std.process::process_error"))
  (type 94 (standard "std.string::boundary_error"))
  (type 95 (standard "std.string::string_error"))
  (type 96 (standard "std.sync::barrier_error"))
  (type 97 (standard "std.thread::thread_error"))
  (type 98 (standard "std.time::duration_error"))
  (type 99 (standard "std.time::time_error"))
  (type 100 (effects (standard "std.time::time_error")))
  (type 101 (effects (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 102 (effects (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 103 (effects (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 104 (effects (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 105 (effects (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 106 (effects (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 107 (effects (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 108 (effects (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 109 (effects (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 110 (effects (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 111 (effects (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 112 (effects (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 113 (effects (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 114 (effects (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 115 (effects (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 116 (effects (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 117 (effects (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 118 (effects (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 119 (effects (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 120 (effects (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 121 (effects (standard "std.bits::read_error") (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 122 (effects (standard "std.async::start_error") (standard "std.bits::read_error") (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 123 (effects (standard "std.alloc::alloc_error") (standard "std.async::start_error") (standard "std.bits::read_error") (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 124 (effects (standard "core::utf8_error") (standard "std.alloc::alloc_error") (standard "std.async::start_error") (standard "std.bits::read_error") (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")))
  (type 125 (carrier i32 (effects (standard "core::utf8_error") (standard "std.alloc::alloc_error") (standard "std.async::start_error") (standard "std.bits::read_error") (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error"))))
  (type 126 (const_borrow i32))
  (type 127 (fixed_array i32 3))
  (type 128 (slice i32))
)
(program
  (module "test.codegen.value_types"
    (function symbol=1 name="echo_bool" visibility=exported return=bool state=definition
      (parameter symbol=30 name="input" type=bool)
      (block type=void
        (local symbol=31 name="value" type=bool
          (load type=bool
            (place symbol=30 name="input" type=bool)
          )
        )
        (return type=bool
          (load type=bool
            (place symbol=31 name="value" type=bool)
          )
        )
      )
    )
    (function symbol=2 name="echo_i8" visibility=exported return=i8 state=definition
      (parameter symbol=32 name="input" type=i8)
      (block type=void
        (local symbol=33 name="value" type=i8
          (load type=i8
            (place symbol=32 name="input" type=i8)
          )
        )
        (return type=i8
          (load type=i8
            (place symbol=33 name="value" type=i8)
          )
        )
      )
    )
    (function symbol=3 name="echo_i16" visibility=exported return=i16 state=definition
      (parameter symbol=34 name="input" type=i16)
      (block type=void
        (local symbol=35 name="value" type=i16
          (load type=i16
            (place symbol=34 name="input" type=i16)
          )
        )
        (return type=i16
          (load type=i16
            (place symbol=35 name="value" type=i16)
          )
        )
      )
    )
    (function symbol=4 name="echo_i32" visibility=exported return=i32 state=definition
      (parameter symbol=36 name="input" type=i32)
      (block type=void
        (local symbol=37 name="value" type=i32
          (load type=i32
            (place symbol=36 name="input" type=i32)
          )
        )
        (return type=i32
          (load type=i32
            (place symbol=37 name="value" type=i32)
          )
        )
      )
    )
    (function symbol=5 name="echo_i64" visibility=exported return=i64 state=definition
      (parameter symbol=38 name="input" type=i64)
      (block type=void
        (local symbol=39 name="value" type=i64
          (load type=i64
            (place symbol=38 name="input" type=i64)
          )
        )
        (return type=i64
          (load type=i64
            (place symbol=39 name="value" type=i64)
          )
        )
      )
    )
    (function symbol=6 name="echo_isize" visibility=exported return=isize state=definition
      (parameter symbol=40 name="input" type=isize)
      (block type=void
        (local symbol=41 name="value" type=isize
          (load type=isize
            (place symbol=40 name="input" type=isize)
          )
        )
        (return type=isize
          (load type=isize
            (place symbol=41 name="value" type=isize)
          )
        )
      )
    )
    (function symbol=7 name="echo_u8" visibility=exported return=u8 state=definition
      (parameter symbol=42 name="input" type=u8)
      (block type=void
        (local symbol=43 name="value" type=u8
          (load type=u8
            (place symbol=42 name="input" type=u8)
          )
        )
        (return type=u8
          (load type=u8
            (place symbol=43 name="value" type=u8)
          )
        )
      )
    )
    (function symbol=8 name="echo_u16" visibility=exported return=u16 state=definition
      (parameter symbol=44 name="input" type=u16)
      (block type=void
        (local symbol=45 name="value" type=u16
          (load type=u16
            (place symbol=44 name="input" type=u16)
          )
        )
        (return type=u16
          (load type=u16
            (place symbol=45 name="value" type=u16)
          )
        )
      )
    )
    (function symbol=9 name="echo_u32" visibility=exported return=u32 state=definition
      (parameter symbol=46 name="input" type=u32)
      (block type=void
        (local symbol=47 name="value" type=u32
          (load type=u32
            (place symbol=46 name="input" type=u32)
          )
        )
        (return type=u32
          (load type=u32
            (place symbol=47 name="value" type=u32)
          )
        )
      )
    )
    (function symbol=10 name="echo_u64" visibility=exported return=u64 state=definition
      (parameter symbol=48 name="input" type=u64)
      (block type=void
        (local symbol=49 name="value" type=u64
          (load type=u64
            (place symbol=48 name="input" type=u64)
          )
        )
        (return type=u64
          (load type=u64
            (place symbol=49 name="value" type=u64)
          )
        )
      )
    )
    (function symbol=11 name="echo_usize" visibility=exported return=usize state=definition
      (parameter symbol=50 name="input" type=usize)
      (block type=void
        (local symbol=51 name="value" type=usize
          (load type=usize
            (place symbol=50 name="input" type=usize)
          )
        )
        (return type=usize
          (load type=usize
            (place symbol=51 name="value" type=usize)
          )
        )
      )
    )
    (function symbol=12 name="echo_f32" visibility=exported return=f32 state=definition
      (parameter symbol=52 name="input" type=f32)
      (block type=void
        (local symbol=53 name="value" type=f32
          (load type=f32
            (place symbol=52 name="input" type=f32)
          )
        )
        (return type=f32
          (load type=f32
            (place symbol=53 name="value" type=f32)
          )
        )
      )
    )
    (function symbol=13 name="echo_f64" visibility=exported return=f64 state=definition
      (parameter symbol=54 name="input" type=f64)
      (block type=void
        (local symbol=55 name="value" type=f64
          (load type=f64
            (place symbol=54 name="input" type=f64)
          )
        )
        (return type=f64
          (load type=f64
            (place symbol=55 name="value" type=f64)
          )
        )
      )
    )
    (function symbol=14 name="echo_char" visibility=exported return=char state=definition
      (parameter symbol=56 name="input" type=char)
      (block type=void
        (local symbol=57 name="value" type=char
          (load type=char
            (place symbol=56 name="input" type=char)
          )
        )
        (return type=char
          (load type=char
            (place symbol=57 name="value" type=char)
          )
        )
      )
    )
    (function symbol=15 name="character_literals" visibility=protected return=(struct "test.codegen.value_types"::"character_literal_set") state=definition
      (block type=void
        (local symbol=58 name="value" type=(struct "test.codegen.value_types"::"character_literal_set")
          (aggregate_init type=(struct "test.codegen.value_types"::"character_literal_set")
            (field_init type=char field=2
              (literal type=char value=65)
            )
            (field_init type=char field=3
              (literal type=char value=233)
            )
            (field_init type=char field=4
              (literal type=char value=128578)
            )
            (field_init type=char field=5
              (literal type=char value=92)
            )
            (field_init type=char field=6
              (literal type=char value=34)
            )
            (field_init type=char field=7
              (literal type=char value=39)
            )
            (field_init type=char field=8
              (literal type=char value=10)
            )
            (field_init type=char field=9
              (literal type=char value=13)
            )
            (field_init type=char field=10
              (literal type=char value=9)
            )
            (field_init type=char field=11
              (literal type=char value=0)
            )
            (field_init type=char field=12
              (literal type=char value=127)
            )
            (field_init type=char field=13
              (literal type=char value=128640)
            )
            (field_init type=char field=14
              (literal type=char value=1114111)
            )
          )
        )
        (return type=(struct "test.codegen.value_types"::"character_literal_set")
          (load type=(struct "test.codegen.value_types"::"character_literal_set")
            (place symbol=58 name="value" type=(struct "test.codegen.value_types"::"character_literal_set"))
          )
        )
      )
    )
    (function symbol=16 name="echo_scalar_tree" visibility=exported return=(struct "test.codegen.value_types"::"scalar_tree") state=definition
      (parameter symbol=59 name="input" type=(struct "test.codegen.value_types"::"scalar_tree"))
      (block type=void
        (local symbol=60 name="value" type=(struct "test.codegen.value_types"::"scalar_tree")
          (load type=(struct "test.codegen.value_types"::"scalar_tree")
            (place symbol=59 name="input" type=(struct "test.codegen.value_types"::"scalar_tree"))
          )
        )
        (return type=(struct "test.codegen.value_types"::"scalar_tree")
          (load type=(struct "test.codegen.value_types"::"scalar_tree")
            (place symbol=60 name="value" type=(struct "test.codegen.value_types"::"scalar_tree"))
          )
        )
      )
    )
    (function symbol=17 name="echo_scalar_option" visibility=exported return=(option i64) state=definition
      (parameter symbol=61 name="input" type=(option i64))
      (block type=void
        (local symbol=62 name="value" type=(option i64)
          (load type=(option i64)
            (place symbol=61 name="input" type=(option i64))
          )
        )
        (return type=(option i64)
          (load type=(option i64)
            (place symbol=62 name="value" type=(option i64))
          )
        )
      )
    )
    (function symbol=18 name="echo_scalar_result" visibility=exported return=(option u16) state=definition
      (parameter symbol=63 name="input" type=(option u16))
      (block type=void
        (local symbol=64 name="value" type=(option u16)
          (load type=(option u16)
            (place symbol=63 name="input" type=(option u16))
          )
        )
        (return type=(option u16)
          (load type=(option u16)
            (place symbol=64 name="value" type=(option u16))
          )
        )
      )
    )
    (function symbol=19 name="default_scalar_values" visibility=exported return=(struct "test.codegen.value_types"::"scalar_defaults") state=definition
      (block type=void
        (local symbol=65 name="value" type=(struct "test.codegen.value_types"::"scalar_defaults")
          (aggregate_init type=(struct "test.codegen.value_types"::"scalar_defaults")
            (field_init type=bool field=16
              (default_value type=bool)
            )
            (field_init type=i8 field=17
              (default_value type=i8)
            )
            (field_init type=i16 field=18
              (default_value type=i16)
            )
            (field_init type=i32 field=19
              (default_value type=i32)
            )
            (field_init type=i64 field=20
              (default_value type=i64)
            )
            (field_init type=isize field=21
              (default_value type=isize)
            )
            (field_init type=u8 field=22
              (default_value type=u8)
            )
            (field_init type=u16 field=23
              (default_value type=u16)
            )
            (field_init type=u32 field=24
              (default_value type=u32)
            )
            (field_init type=u64 field=25
              (default_value type=u64)
            )
            (field_init type=usize field=26
              (default_value type=usize)
            )
            (field_init type=f32 field=27
              (default_value type=f32)
            )
            (field_init type=f64 field=28
              (default_value type=f64)
            )
            (field_init type=char field=29
              (default_value type=char)
            )
            (field_init type=(fixed_array u16 2) field=30
              (default_value type=(fixed_array u16 2))
            )
          )
        )
        (return type=(struct "test.codegen.value_types"::"scalar_defaults")
          (load type=(struct "test.codegen.value_types"::"scalar_defaults")
            (place symbol=65 name="value" type=(struct "test.codegen.value_types"::"scalar_defaults"))
          )
        )
      )
    )
    (function symbol=20 name="contextual_scalar_literals" visibility=exported return=(struct "test.codegen.value_types"::"scalar_integers") state=definition
      (block type=void
        (local symbol=66 name="value" type=(struct "test.codegen.value_types"::"scalar_integers")
          (aggregate_init type=(struct "test.codegen.value_types"::"scalar_integers")
            (field_init type=i8 field=31
              (literal type=i8 value=128)
            )
            (field_init type=i16 field=32
              (literal type=i16 value=32768)
            )
            (field_init type=i32 field=33
              (literal type=i32 value=2147483648)
            )
            (field_init type=i64 field=34
              (literal type=i64 value=9223372036854775808)
            )
            (field_init type=isize field=35
              (literal type=isize value=9223372036854775808)
            )
            (field_init type=u8 field=36
              (literal type=u8 value=255)
            )
            (field_init type=u16 field=37
              (literal type=u16 value=65535)
            )
            (field_init type=u32 field=38
              (literal type=u32 value=4294967295)
            )
            (field_init type=u64 field=39
              (literal type=u64 value=18446744073709551615)
            )
            (field_init type=usize field=40
              (literal type=usize value=18446744073709551615)
            )
          )
        )
        (return type=(struct "test.codegen.value_types"::"scalar_integers")
          (load type=(struct "test.codegen.value_types"::"scalar_integers")
            (place symbol=66 name="value" type=(struct "test.codegen.value_types"::"scalar_integers"))
          )
        )
      )
    )
    (function symbol=21 name="suffixed_scalar_literals" visibility=exported return=(struct "test.codegen.value_types"::"scalar_integers") state=definition
      (block type=void
        (local symbol=67 name="value" type=(struct "test.codegen.value_types"::"scalar_integers")
          (aggregate_init type=(struct "test.codegen.value_types"::"scalar_integers")
            (field_init type=i8 field=31
              (literal type=i8 value=128)
            )
            (field_init type=i16 field=32
              (literal type=i16 value=32768)
            )
            (field_init type=i32 field=33
              (literal type=i32 value=2147483648)
            )
            (field_init type=i64 field=34
              (literal type=i64 value=9223372036854775808)
            )
            (field_init type=isize field=35
              (literal type=isize value=9223372036854775808)
            )
            (field_init type=u8 field=36
              (literal type=u8 value=255)
            )
            (field_init type=u16 field=37
              (literal type=u16 value=65535)
            )
            (field_init type=u32 field=38
              (literal type=u32 value=4294967295)
            )
            (field_init type=u64 field=39
              (literal type=u64 value=18446744073709551615)
            )
            (field_init type=usize field=40
              (literal type=usize value=18446744073709551615)
            )
          )
        )
        (return type=(struct "test.codegen.value_types"::"scalar_integers")
          (load type=(struct "test.codegen.value_types"::"scalar_integers")
            (place symbol=67 name="value" type=(struct "test.codegen.value_types"::"scalar_integers"))
          )
        )
      )
    )
    (function symbol=22 name="maybe_value" visibility=protected return=(option i32) state=definition
      (parameter symbol=68 name="present" type=bool)
      (block type=void
        (if type=void
          (binary op="==" type=bool
            (load type=bool
              (place symbol=68 name="present" type=bool)
            )
            (literal type=bool value=1)
          )
          (block type=void
            (local symbol=69 name="value" type=(option i32)
              (variant type=(option i32) tag=1
                (literal type=i32 value=7)
              )
            )
            (return type=(option i32)
              (load type=(option i32)
                (place symbol=69 name="value" type=(option i32))
              )
            )
          )
        )
        (local symbol=70 name="absent" type=(option i32)
          (variant type=(option i32) tag=0)
        )
        (return type=(option i32)
          (load type=(option i32)
            (place symbol=70 name="absent" type=(option i32))
          )
        )
      )
    )
    (function symbol=23 name="propagate_option" visibility=protected return=(option i32) state=definition
      (parameter symbol=71 name="present" type=bool)
      (block type=void
        (local symbol=72 name="candidate" type=(option i32)
          (call symbol=22 name="maybe_value" type=(option i32)
            (load type=bool
              (place symbol=71 name="present" type=bool)
            )
          )
        )
        (switch type=void
          (load type=(option i32)
            (place symbol=72 name="candidate" type=(option i32))
          )
          (case pattern=variant value=1 op="return" symbol=73 name="value" type=(const_borrow i32) borrow_origin=72
            (block type=void
              (local symbol=74 name="success" type=(option i32)
                (variant type=(option i32) tag=1 borrow_origin=72
                  (load type=i32 borrow_origin=72
                    (deref_place type=i32 borrow_origin=72
                      (place symbol=73 name="value" type=(const_borrow i32) borrow_origin=72)
                    )
                  )
                )
              )
              (return type=(option i32)
                (load type=(option i32)
                  (place symbol=74 name="success" type=(option i32))
                )
              )
            )
          )
          (case pattern=variant value=0 op="return"
            (block type=void
              (local symbol=75 name="absent" type=(option i32)
                (variant type=(option i32) tag=0)
              )
              (return type=(option i32)
                (load type=(option i32)
                  (place symbol=75 name="absent" type=(option i32))
                )
              )
            )
          )
        )
      )
    )
    (function symbol=24 name="checked_value" visibility=protected return=i32 throws=(effects (struct "test.codegen.value_types"::"issue")) state=definition
      (parameter symbol=76 name="fail" type=bool)
      (block type=void
        (if type=void
          (binary op="==" type=bool
            (load type=bool
              (place symbol=76 name="fail" type=bool)
            )
            (literal type=bool value=1)
          )
          (block type=void
            (throw error=(struct "test.codegen.value_types"::"issue") propagate
              (aggregate_init type=(struct "test.codegen.value_types"::"issue")
                (field_init type=i32 field=15
                  (literal type=i32 value=9)
                )
              )
            )
          )
        )
        (return type=i32
          (literal type=i32 value=4)
        )
      )
    )
    (function symbol=25 name="named_error" visibility=protected return=i32 throws=(effects (struct "test.codegen.value_types"::"issue")) state=definition
      (block type=void
        (local symbol=77 name="error" type=(struct "test.codegen.value_types"::"issue")
          (aggregate_init type=(struct "test.codegen.value_types"::"issue")
            (field_init type=i32 field=15
              (literal type=i32 value=12)
            )
          )
        )
        (throw error=(struct "test.codegen.value_types"::"issue") propagate
          (load type=(struct "test.codegen.value_types"::"issue")
            (place symbol=77 name="error" type=(struct "test.codegen.value_types"::"issue"))
          )
        )
      )
    )
    (function symbol=26 name="void_status" visibility=protected return=void throws=(effects (struct "test.codegen.value_types"::"issue")) state=definition
      (parameter symbol=78 name="fail" type=bool)
      (block type=void
        (if type=void
          (binary op="==" type=bool
            (load type=bool
              (place symbol=78 name="fail" type=bool)
            )
            (literal type=bool value=1)
          )
          (block type=void
            (throw error=(struct "test.codegen.value_types"::"issue") propagate
              (aggregate_init type=(struct "test.codegen.value_types"::"issue")
                (field_init type=i32 field=15
                  (literal type=i32 value=13)
                )
              )
            )
          )
        )
      )
    )
    (function symbol=27 name="propagate_result" visibility=protected return=i32 throws=(effects (struct "test.codegen.value_types"::"issue")) state=definition
      (parameter symbol=79 name="fail" type=bool)
      (block type=void
        (expression_statement type=void
          (call symbol=26 name="void_status" type=void
            (load type=bool
              (place symbol=79 name="fail" type=bool)
            )
          )
        )
        (local symbol=80 name="value" type=i32
          (call symbol=24 name="checked_value" type=i32
            (load type=bool
              (place symbol=79 name="fail" type=bool)
            )
          )
        )
        (return type=i32
          (load type=i32
            (place symbol=80 name="value" type=i32)
          )
        )
      )
    )
    (function symbol=28 name="read_at" visibility=protected return=i32 state=definition
      (parameter symbol=81 name="values" type=(const_slice i32))
      (parameter symbol=82 name="index" type=u32)
      (block type=void
        (return type=i32
          (load type=i32
            (index_place type=i32 bound=dynamic readonly
              (place symbol=81 name="values" type=(const_slice i32))
              (cast type=usize implicit borrow_origin=82
                (load type=u32
                  (place symbol=82 name="index" type=u32)
                )
              )
            )
          )
        )
      )
    )
    (function symbol=29 name="main" visibility=exported return=i32 throws=(effects (standard "core::utf8_error") (standard "std.alloc::alloc_error") (standard "std.async::start_error") (standard "std.bits::read_error") (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error")) state=definition
      (block type=void
        (try op="try" type=void propagate=(effects (standard "core::utf8_error") (standard "std.alloc::alloc_error") (standard "std.async::start_error") (standard "std.bits::read_error") (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error"))
          (block type=void
            (local symbol=83 name="characters" type=(struct "test.codegen.value_types"::"character_literal_set")
              (aggregate_init type=(struct "test.codegen.value_types"::"character_literal_set")
                (field_init type=char field=2
                  (literal type=char value=65)
                )
                (field_init type=char field=3
                  (literal type=char value=233)
                )
                (field_init type=char field=4
                  (literal type=char value=128578)
                )
                (field_init type=char field=5
                  (literal type=char value=92)
                )
                (field_init type=char field=6
                  (literal type=char value=34)
                )
                (field_init type=char field=7
                  (literal type=char value=39)
                )
                (field_init type=char field=8
                  (literal type=char value=10)
                )
                (field_init type=char field=9
                  (literal type=char value=13)
                )
                (field_init type=char field=10
                  (literal type=char value=9)
                )
                (field_init type=char field=11
                  (literal type=char value=0)
                )
                (field_init type=char field=12
                  (literal type=char value=127)
                )
                (field_init type=char field=13
                  (literal type=char value=128640)
                )
                (field_init type=char field=14
                  (literal type=char value=1114111)
                )
              )
            )
            (expression_statement type=void
              (discard type=void
                (load type=(struct "test.codegen.value_types"::"character_literal_set")
                  (place symbol=83 name="characters" type=(struct "test.codegen.value_types"::"character_literal_set"))
                )
              )
            )
            (local symbol=84 name="values" type=(fixed_array i32 3)
              (array_init type=(fixed_array i32 3) length=3
                (literal type=i32 value=1)
                (literal type=i32 value=2)
              )
            )
            (local symbol=85 name="mutable_values" type=(slice i32) borrow_origin=84
              (slice type=(slice i32) length=3
                (place symbol=84 name="values" type=(fixed_array i32 3))
              )
            )
            (expression_statement type=void
              (compound_assign op="+=" type=void
                (index_place type=i32 bound=dynamic borrow_origin=84
                  (place symbol=85 name="mutable_values" type=(slice i32) borrow_origin=84)
                  (literal type=usize value=1)
                )
                (literal type=i32 value=3)
              )
            )
            (local symbol=86 name="first" type=i32
              (load type=i32
                (index_place type=i32 bound=3
                  (place symbol=84 name="values" type=(fixed_array i32 3))
                  (literal type=usize value=0)
                )
              )
            )
            (local symbol=87 name="view" type=(const_slice i32) borrow_origin=84
              (slice type=(const_slice i32) length=3
                (place symbol=84 name="values" type=(fixed_array i32 3))
              )
            )
            (local symbol=88 name="selected" type=i32
              (call symbol=28 name="read_at" type=i32
                (load type=(const_slice i32) borrow_origin=84
                  (place symbol=87 name="view" type=(const_slice i32) borrow_origin=84)
                )
                (literal type=u32 value=1)
              )
            )
            (expression_statement type=void
              (discard type=void
                (load type=i32
                  (place symbol=88 name="selected" type=i32)
                )
              )
            )
            (local symbol=89 name="option_result" type=(option i32)
              (call symbol=23 name="propagate_option" type=(option i32)
                (binary op="==" type=bool
                  (load type=i32
                    (place symbol=88 name="selected" type=i32)
                  )
                  (literal type=i32 value=5)
                )
              )
            )
            (local symbol=90 name="option_value" type=i32
              (literal type=i32 value=0)
            )
            (switch type=void
              (load type=(option i32)
                (place symbol=89 name="option_result" type=(option i32))
              )
              (case pattern=variant value=1 op="break" symbol=91 name="value" type=(const_borrow i32) borrow_origin=89
                (block type=void
                  (expression_statement type=void
                    (assign op="=" type=void
                      (place symbol=90 name="option_value" type=i32)
                      (load type=i32 borrow_origin=89
                        (deref_place type=i32 borrow_origin=89
                          (place symbol=91 name="value" type=(const_borrow i32) borrow_origin=89)
                        )
                      )
                    )
                  )
                )
              )
              (case pattern=variant value=0 op="break"
                (block type=void
                  (expression_statement type=void
                    (assign op="=" type=void
                      (place symbol=90 name="option_value" type=i32)
                      (unary op="-" type=i32
                        (literal type=i32 value=1)
                      )
                    )
                  )
                )
              )
            )
            (try op="try" type=void propagate=(effects (standard "core::utf8_error") (standard "std.alloc::alloc_error") (standard "std.async::start_error") (standard "std.bits::read_error") (standard "std.bytes::bytes_error") (standard "std.c::runtime_error") (standard "std.c::string_error") (standard "std.convert::parse_error") (standard "std.convert::range_error") (standard "std.env::env_error") (standard "std.error::error") (standard "std.format::format_error") (standard "std.fs::fs_error") (standard "std.fs::path_error") (standard "std.io::io_error") (standard "std.math::math_error") (standard "std.net::address_error") (standard "std.net::net_error") (standard "std.process::process_error") (standard "std.string::boundary_error") (standard "std.string::string_error") (standard "std.sync::barrier_error") (standard "std.thread::thread_error") (standard "std.time::duration_error") (standard "std.time::time_error"))
              (block type=void
                (local symbol=92 name="explicit_ok" type=i32
                  (call symbol=24 name="checked_value" type=i32
                    (binary op="!=" type=bool
                      (load type=i32
                        (place symbol=88 name="selected" type=i32)
                      )
                      (literal type=i32 value=5)
                    )
                  )
                )
                (if type=void
                  (binary op="!=" type=bool
                    (load type=i32
                      (place symbol=92 name="explicit_ok" type=i32)
                    )
                    (literal type=i32 value=4)
                  )
                  (block type=void
                    (throw error=(struct "test.codegen.value_types"::"TestAssertionFailed") caught
                      (aggregate_init type=(struct "test.codegen.value_types"::"TestAssertionFailed")
                        (field_init type=i32 field=1
                          (literal type=i32 value=2)
                        )
                      )
                    )
                  )
                )
                (local symbol=93 name="value" type=i32
                  (call symbol=27 name="propagate_result" type=i32
                    (binary op="!=" type=bool
                      (load type=i32
                        (place symbol=88 name="selected" type=i32)
                      )
                      (literal type=i32 value=5)
                    )
                  )
                )
                (if type=void
                  (binary op="==" type=bool
                    (load type=i32
                      (place symbol=88 name="selected" type=i32)
                    )
                    (literal type=i32 value=5)
                  )
                  (block type=void
                    (if type=void
                      (binary op="==" type=bool
                        (load type=i32
                          (place symbol=86 name="first" type=i32)
                        )
                        (literal type=i32 value=1)
                      )
                      (block type=void
                        (if type=void
                          (binary op="==" type=bool
                            (load type=i32
                              (place symbol=90 name="option_value" type=i32)
                            )
                            (literal type=i32 value=7)
                          )
                          (block type=void
                            (return type=i32
                              (binary op="-" type=i32
                                (load type=i32
                                  (place symbol=93 name="value" type=i32)
                                )
                                (literal type=i32 value=4)
                              )
                            )
                          )
                        )
                      )
                    )
                  )
                )
                (throw error=(struct "test.codegen.value_types"::"TestAssertionFailed") caught
                  (aggregate_init type=(struct "test.codegen.value_types"::"TestAssertionFailed")
                    (field_init type=i32 field=1
                      (literal type=i32 value=1)
                    )
                  )
                )
              )
              (catch symbol=94 name="error" type=(struct "test.codegen.value_types"::"issue")
                (block type=void
                  (block type=void
                    (return type=i32
                      (load type=i32
                        (field_place type=i32 field=15
                          (place symbol=94 name="error" type=(struct "test.codegen.value_types"::"issue"))
                        )
                      )
                    )
                  )
                )
              )
            )
          )
          (catch symbol=95 name="failure" type=(struct "test.codegen.value_types"::"TestAssertionFailed")
            (block type=void
              (block type=void
                (return type=i32
                  (load type=i32
                    (field_place type=i32 field=1
                      (place symbol=95 name="failure" type=(struct "test.codegen.value_types"::"TestAssertionFailed"))
                    )
                  )
                )
              )
            )
          )
        )
      )
    )
  )
)
