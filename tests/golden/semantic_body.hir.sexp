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
)
(program
  (module "semantic.body"
    (function symbol=1 name="add" visibility=protected return=i32 state=definition
      (parameter symbol=4 name="left" type=i32)
      (parameter symbol=5 name="right" type=i32)
      (block type=void
        (local symbol=6 name="sum" type=i32
          (binary op="+" type=i32
            (load type=i32
              (place symbol=4 name="left" type=i32)
            )
            (load type=i32
              (place symbol=5 name="right" type=i32)
            )
          )
        )
        (return type=i32
          (load type=i32
            (place symbol=6 name="sum" type=i32)
          )
        )
      )
    )
    (function symbol=2 name="both" visibility=protected return=bool state=definition
      (parameter symbol=7 name="left" type=bool)
      (parameter symbol=8 name="right" type=bool)
      (block type=void
        (return type=bool
          (binary op="&&" type=bool
            (load type=bool
              (place symbol=7 name="left" type=bool)
            )
            (load type=bool
              (place symbol=8 name="right" type=bool)
            )
          )
        )
      )
    )
    (function symbol=3 name="compute" visibility=protected return=i32 state=definition
      (parameter symbol=9 name="input" type=i32)
      (block type=void
        (local symbol=10 name="value" type=i32
          (load type=i32
            (place symbol=9 name="input" type=i32)
          )
        )
        (expression_statement type=void
          (compound_assign op="+=" type=void
            (place symbol=10 name="value" type=i32)
            (literal type=i32 value=2)
          )
        )
        (if type=void
          (binary op=">" type=bool
            (load type=i32
              (place symbol=10 name="value" type=i32)
            )
            (literal type=i32 value=10)
          )
          (block type=void
            (expression_statement type=void
              (assign op="=" type=void
                (place symbol=10 name="value" type=i32)
                (binary op="-" type=i32
                  (load type=i32
                    (place symbol=10 name="value" type=i32)
                  )
                  (literal type=i32 value=1)
                )
              )
            )
          )
          (block type=void
            (expression_statement type=void
              (assign op="=" type=void
                (place symbol=10 name="value" type=i32)
                (unary op="-" type=i32
                  (load type=i32
                    (place symbol=10 name="value" type=i32)
                  )
                )
              )
            )
          )
        )
        (while type=void
          (binary op="<" type=bool
            (load type=i32
              (place symbol=10 name="value" type=i32)
            )
            (literal type=i32 value=0)
          )
          (block type=void
            (expression_statement type=void
              (compound_assign op="+=" type=void
                (place symbol=10 name="value" type=i32)
                (literal type=i32 value=1)
              )
            )
          )
        )
        (local symbol=11 name="result" type=i32
          (call symbol=1 name="add" type=i32
            (load type=i32
              (place symbol=10 name="value" type=i32)
            )
            (literal type=i32 value=1)
          )
        )
        (return type=i32
          (load type=i32
            (place symbol=11 name="result" type=i32)
          )
        )
      )
    )
  )
)
