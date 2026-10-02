(translation_unit
  (module_declaration
    (token module "module")
    (token whitespace " ")
    (token identifier "golden")
    (token ; ";")
  )
  (token whitespace "\n\n")
  (external_declaration
    (error_struct_declaration
      (token identifier "error")
      (token whitespace " ")
      (token identifier "Error")
      (token whitespace " ")
      (token { "{")
      (token whitespace "\n    ")
      (field_declaration
        (type
          (token i32 "i32")
        )
        (token whitespace " ")
        (token identifier "code")
        (token ; ";")
      )
      (token whitespace "\n")
      (token } "}")
      (token ; ";")
    )
  )
  (token whitespace "\n\n")
  (external_declaration
    (token protected "protected")
    (token whitespace " ")
    (function_declaration
      (type
        (token i32 "i32")
      )
      (token whitespace " ")
      (token identifier "identity")
      (parameter_list
        (token ( "(")
        (parameter
          (type
            (token i32 "i32")
          )
          (token whitespace " ")
          (token identifier "value")
        )
        (token ) ")")
      )
      (token whitespace " ")
      (throws_clause
        (token throws "throws")
        (token whitespace " ")
        (type
          (token identifier "Error")
        )
      )
      (token whitespace " ")
      (block
        (token { "{")
        (token whitespace "\n    ")
        (try_statement
          (token try "try")
          (token whitespace " ")
          (block
            (token { "{")
            (token whitespace "\n        ")
            (if_statement
              (token if "if")
              (token whitespace " ")
              (token ( "(")
              (expression
                (binary_expression
                  (binary_expression
                    (postfix_expression
                      (primary_expression
                        (token identifier "value")
                      )
                    )
                  )
                  (token whitespace " ")
                  (token == "==")
                  (token whitespace " ")
                  (binary_expression
                    (postfix_expression
                      (primary_expression
                        (token integer_literal "0")
                      )
                    )
                  )
                )
              )
              (token ) ")")
              (token whitespace " ")
              (block
                (token { "{")
                (token whitespace "\n            ")
                (throw_statement
                  (token throw "throw")
                  (token whitespace " ")
                  (aggregate_initializer
                    (token { "{")
                    (token whitespace "\n                ")
                    (initializer_item
                      (token . ".")
                      (token identifier "code")
                      (token whitespace " ")
                      (token = "=")
                      (token whitespace " ")
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
                    (token , ",")
                    (token whitespace "\n            ")
                    (token } "}")
                  )
                  (token ; ";")
                )
                (token whitespace "\n        ")
                (token } "}")
              )
            )
            (token whitespace "\n        ")
            (jump_statement
              (token return "return")
              (token whitespace " ")
              (expression
                (binary_expression
                  (postfix_expression
                    (primary_expression
                      (token identifier "value")
                    )
                  )
                )
              )
              (token ; ";")
            )
            (token whitespace "\n    ")
            (token } "}")
          )
          (token whitespace " ")
          (catch_clause
            (token catch "catch")
            (token whitespace " ")
            (token ( "(")
            (type
              (token identifier "Error")
            )
            (token whitespace " ")
            (token identifier "error")
            (token ) ")")
            (token whitespace " ")
            (block
              (token { "{")
              (token whitespace "\n        ")
              (throw_statement
                (token throw "throw")
                (token ; ";")
              )
              (token whitespace "\n    ")
              (token } "}")
            )
          )
          (token whitespace " ")
          (finally_clause
            (token finally "finally")
            (token whitespace " ")
            (block
              (token { "{")
              (token whitespace "\n        ")
              (expression_statement
                (token ; ";")
              )
              (token whitespace "\n    ")
              (token } "}")
            )
          )
        )
        (token whitespace "\n")
        (token } "}")
      )
    )
  )
  (token whitespace "\n\n")
  (external_declaration
    (token protected "protected")
    (token whitespace " ")
    (token async "async")
    (token whitespace " ")
    (function_declaration
      (type
        (token i32 "i32")
      )
      (token whitespace " ")
      (token identifier "wait")
      (parameter_list
        (token ( "(")
        (parameter
          (type
            (token task "task")
            (token < "<")
            (type
              (token i32 "i32")
            )
            (token whitespace " ")
            (throws_clause
              (token throws "throws")
              (token whitespace " ")
              (type
                (token identifier "Error")
              )
            )
            (token > ">")
          )
          (token whitespace " ")
          (token identifier "operation")
        )
        (token ) ")")
      )
      (token whitespace " ")
      (throws_clause
        (token throws "throws")
        (token whitespace " ")
        (type
          (token identifier "Error")
        )
      )
      (token whitespace " ")
      (block
        (token { "{")
        (token whitespace "\n    ")
        (object_declaration
          (type
            (token i32 "i32")
          )
          (token whitespace " ")
          (token identifier "value")
          (token whitespace " ")
          (token = "=")
          (token whitespace " ")
          (initializer
            (expression
              (binary_expression
                (await_operation
                  (token await "await")
                  (token whitespace " ")
                  (token move "move")
                  (token whitespace " ")
                  (token identifier "operation")
                )
              )
            )
          )
          (token ; ";")
        )
        (token whitespace "\n    ")
        (jump_statement
          (token return "return")
          (token whitespace " ")
          (expression
            (binary_expression
              (postfix_expression
                (primary_expression
                  (token identifier "value")
                )
              )
            )
          )
          (token ; ";")
        )
        (token whitespace "\n")
        (token } "}")
      )
    )
  )
  (token whitespace "\n")
  (token eof "")
)
