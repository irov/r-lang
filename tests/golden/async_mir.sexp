(mir version=1 core_revision="0.1.0-draft.102"
  (function name="test.async_mir::consume" visibility=exported return=i32 async=true start=(carrier (task i32) (effects (standard "std.async::start_error"))) state=definition
    (block bb0
      (parameter place=%arg0 name="work" type=(task i32))
      (local place=%local0 name="value" type=i32)
      (jump target=bb1)
    )
    (block bb1
      (%v0 = await source=%arg0 task=(task i32) type=i32 resume=bb2 cancel=bb3 consuming panic=bb4)
    )
    (block bb2
      (store place=%local0 value=%v0)
      (%v1 = load place=%local0 type=i32)
      (return value=%v1)
    )
    (block bb3
      (cancel)
    )
    (block bb4
      (panic)
    )
  )
  (function name="test.async_mir::drain" visibility=exported return=void async=true start=(carrier (task void) (effects (standard "std.async::start_error"))) state=definition
    (block bb0
      (parameter place=%arg0 name="work" type=(task void))
      (jump target=bb1)
    )
    (block bb1
      (await source=%arg0 task=(task void) type=void resume=bb2 cancel=bb3 consuming panic=bb4)
    )
    (block bb2
      (return)
    )
    (block bb3
      (cancel)
    )
    (block bb4
      (panic)
    )
  )
  (function name="test.async_mir::produce" visibility=exported return=i32 async=true start=(carrier (task i32) (effects (standard "std.async::start_error"))) state=definition
    (block bb0
      (parameter place=%arg0 name="value" type=i32)
      (%v0 = load place=%arg0 type=i32)
      (return value=%v0)
    )
  )
  (function name="test.async_mir::start" visibility=protected return=(task i32) state=definition
    (block bb0
      (local place=%local0 name="started" type=(task i32))
      (%v0 = constant type=i32 value=7)
      (%v1 = async_start callee="test.async_mir::produce" arguments=(%v0) type=(carrier (task i32) (effects (standard "std.async::start_error"))) task=(task i32))
      (%v2 = effect_tag carrier=%v1 type=(carrier (task i32) (effects (standard "std.async::start_error"))))
      (%v3 = constant type=u32 value=1)
      (%v4 = binary op="==" left=%v2 right=%v3 type=bool)
      (branch condition=%v4 then=bb1 else=bb2)
    )
    (block bb1
      (%v5 = effect_payload carrier=%v1 tag=1 type=(standard "std.async::start_error"))
      (throw value=%v5 error=(standard "std.async::start_error") propagate_tag=1)
    )
    (block bb2
      (%v6 = effect_payload carrier=%v1 tag=0 type=(task i32))
      (store place=%local0 value=%v6)
      (%v7 = move source=%local0 type=(task i32))
      (return value=%v7)
    )
  )
)
