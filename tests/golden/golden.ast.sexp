(translation_unit
  (module_declaration
    (token module "module")
    (token identifier "golden")
  )
  (external_declaration
    (error_struct_declaration
      (token identifier "error")
      (token identifier "Error")
      (field_declaration
        (type
          (token i32 "i32")
        )
        (token identifier "code")
      )
    )
  )
  (external_declaration
    (token protected "protected")
    (function_declaration
      (type
        (token i32 "i32")
      )
      (token identifier "identity")
      (parameter_list
        (parameter
          (type
            (token i32 "i32")
          )
          (token identifier "value")
        )
      )
      (throws_clause
        (token throws "throws")
        (type
          (token identifier "Error")
        )
      )
      (block
        (try_statement
          (token try "try")
          (block
            (if_statement
              (token if "if")
              (expression
                (binary_expression
                  (binary_expression
                    (postfix_expression
                      (primary_expression
                        (token identifier "value")
                      )
                    )
                  )
                  (token == "==")
                  (binary_expression
                    (postfix_expression
                      (primary_expression
                        (token integer_literal "0")
                      )
                    )
                  )
                )
              )
              (block
                (throw_statement
                  (token throw "throw")
                  (aggregate_initializer
                    (initializer_item
                      (token identifier "code")
                      (token = "=")
                      (initializer
                        (expression
                          (binary_expression
                            (postfix_expression
                              (primary_expression
                                (token identifier "value")
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
            (jump_statement
              (token return "return")
              (expression
                (binary_expression
                  (postfix_expression
                    (primary_expression
                      (token identifier "value")
                    )
                  )
                )
              )
            )
          )
          (catch_clause
            (token catch "catch")
            (type
              (token identifier "Error")
            )
            (token identifier "error")
            (block
              (throw_statement
                (token throw "throw")
              )
            )
          )
          (finally_clause
            (token finally "finally")
            (block
              (expression_statement)
            )
          )
        )
      )
    )
  )
  (external_declaration
    (token protected "protected")
    (token async "async")
    (function_declaration
      (type
        (token i32 "i32")
      )
      (token identifier "wait")
      (parameter_list
        (parameter
          (type
            (token task "task")
            (type
              (token i32 "i32")
            )
            (throws_clause
              (token throws "throws")
              (type
                (token identifier "Error")
              )
            )
          )
          (token identifier "operation")
        )
      )
      (throws_clause
        (token throws "throws")
        (type
          (token identifier "Error")
        )
      )
      (block
        (object_declaration
          (type
            (token i32 "i32")
          )
          (token identifier "value")
          (token = "=")
          (initializer
            (expression
              (binary_expression
                (await_operation
                  (token await "await")
                  (token move "move")
                  (token identifier "operation")
                )
              )
            )
          )
        )
        (jump_statement
          (token return "return")
          (expression
            (binary_expression
              (postfix_expression
                (primary_expression
                  (token identifier "value")
                )
              )
            )
          )
        )
      )
    )
  )
)
