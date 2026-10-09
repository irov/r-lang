#!/usr/bin/env python3
"""Generate the runtime surface tables of the LLVM backend (B2 of the LLVM transition).

A backend that emits LLVM IR for calls into the runtime and the library, which are C, needs what
a C compiler knows about them: the layout of every type, the C type of every function and how the
C ABI of the target passes it. For the headers a program may reach, this tool compiles probes with the pinned clang for the manifest target
(`toolchain.llvm_triple`) and records, without executing anything:

* every function the headers declare with external linkage: its C type, and the declaration
  clang lowers it to (`-emit-llvm`), the oracle of the backend's own C ABI lowering (B3);
* every struct, union and enumeration reached from those functions or named by a typedef of the
  headers, transitively through members: size, alignment, members with offsets and types (a
  member of an anonymous struct or union is a member of the enclosing record, C11 6.7.2.1),
  enumerators, and the compatible integer type of an enumeration;
* the typedef names and the enumerators of the headers.

A function the headers define `static inline` has no symbol a backend could call. For each, the
tool also writes an external function `r_shim_<name>` of the same type that calls it, in
runtime/llvm/inline_shims.generated.c, and the surface lists the shim in its place; the LLVM
backend calls the shim (until the runtime and library are linked as bitcode, B7).

It writes compiler/llvm/runtime_surface.generated.inc and runtime/llvm/inline_shims.generated.c;
`--verify` fails when either is stale.
"""

from __future__ import annotations

import argparse
import glob
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_c_abi_record import (  # noqa: E402
    index_tag_declarations,
    normalize_type,
    type_children,
    walk,
)

OUTPUT = Path("compiler/llvm/runtime_surface.generated.inc")
SHIMS = Path("runtime/llvm/inline_shims.generated.c")
SHIM_PREFIX = "r_shim_"
INCLUDE_ROOTS = ("runtime/include", "runtime/darwin/include", "library/core/include")
# The runtime and library headers a program may reach; check_header_list keeps it complete.
HEADERS = (
    "r_core.h",
    "r_runtime_0_1.h",
    "r_runtime_arc.h",
    "r_runtime_array.h",
    "r_runtime_dict.h",
    "r_runtime_drop.h",
    "r_runtime_list.h",
    "r_runtime_own.h",
    "r_runtime_rc.h",
    "r_runtime_target_abi.h",
    "r_std_alloc.h",
    "r_std_arc.h",
    "r_std_array.h",
    "r_std_async.h",
    "r_std_bits.h",
    "r_std_bytes.h",
    "r_std_c.h",
    "r_std_convert.h",
    "r_std_dict.h",
    "r_std_env.h",
    "r_std_error.h",
    "r_std_error_types.h",
    "r_std_format.h",
    "r_std_fs.h",
    "r_std_hash.h",
    "r_std_io.h",
    "r_std_json.h",
    "r_std_json_reader.h",
    "r_std_list.h",
    "r_std_math.h",
    "r_std_net.h",
    "r_std_process.h",
    "r_std_random.h",
    "r_std_rc.h",
    "r_std_secret.h",
    "r_std_signal.h",
    "r_std_string.h",
    "r_std_sync.h",
    "r_std_test.h",
    "r_std_thread.h",
    "r_std_time.h",
    "r_std_utf8.h",
)
KIND = {
    "void": "R_LLVM_SURFACE_VOID",
    "bool": "R_LLVM_SURFACE_BOOL",
    "integer": "R_LLVM_SURFACE_INTEGER",
    "float": "R_LLVM_SURFACE_FLOAT",
    "pointer": "R_LLVM_SURFACE_POINTER",
    "function": "R_LLVM_SURFACE_FUNCTION",
    "array": "R_LLVM_SURFACE_ARRAY",
    "struct": "R_LLVM_SURFACE_STRUCT",
    "union": "R_LLVM_SURFACE_UNION",
    "enum": "R_LLVM_SURFACE_ENUM",
    "opaque": "R_LLVM_SURFACE_OPAQUE",
}
FLOATS = ("float", "double", "long double")
ENUM_CANDIDATES = (
    "int",
    "unsigned int",
    "long",
    "unsigned long",
    "long long",
    "unsigned long long",
    "short",
    "unsigned short",
    "char",
    "signed char",
    "unsigned char",
)


class SurfaceError(Exception):
    pass


def compile_text(cc: str, arguments: list[str], source: str, directory: Path, name: str,
                 extra: list[str]) -> subprocess.CompletedProcess[str]:
    path = directory / name
    path.write_text(source, encoding="utf-8")
    return subprocess.run([cc, *arguments, *extra, str(path)], capture_output=True, text=True,
                          check=False)


def ast(cc: str, arguments: list[str], source: str, directory: Path, name: str) -> dict[str, Any]:
    completed = compile_text(cc, arguments, source, directory, name,
                             ["-fsyntax-only", "-Xclang", "-ast-dump=json"])
    if completed.returncode != 0:
        raise SurfaceError(f"{name} does not compile:\n{completed.stderr}")
    return json.loads(completed.stdout)


def constant_values(tree: dict[str, Any]) -> dict[str, int]:
    values = {}
    for node in walk(tree):
        if node.get("kind") == "EnumConstantDecl":
            for expression in node.get("inner", []) or []:
                if expression.get("kind") == "ConstantExpr" and "value" in expression:
                    values[node["name"]] = int(expression["value"])
    return values


def in_project(path: str, root: Path) -> bool:
    if not path:
        return False
    try:
        relative = Path(path).resolve().relative_to(root)
    except ValueError:
        return False
    return relative.parts[0] in ("runtime", "library")


def top_level_in_project(tree: dict[str, Any], root: Path):
    """The top-level declarations of the AST that the project headers declare."""
    current = ""
    for node in tree.get("inner", []) or []:
        location = node.get("loc", {})
        if "file" in location:
            current = location["file"]
        if in_project(current, root) and not node.get("isImplicit"):
            yield node


def surface_tags(tree: dict[str, Any]) -> dict[str, dict[str, Any]]:
    """The complete declarations by AST id, also under the ids of the earlier declarations they
    complete: a typedef of a struct declared before its definition names the earlier one."""
    tags = index_tag_declarations(tree)
    for node in walk(tree):
        previous = node.get("previousDecl")
        if previous and node.get("id") in tags and previous not in tags:
            tags[previous] = tags[node["id"]]
    return tags


def typedef_trees(tree: dict[str, Any], prefix: str) -> dict[str, dict[str, Any]]:
    tags = surface_tags(tree)
    result = {}
    for node in tree.get("inner", []) or []:
        name = node.get("name", "")
        if node.get("kind") == "TypedefDecl" and name.startswith(prefix):
            children = type_children(node)
            if not children:
                raise SurfaceError(f"type probe {name} has no type")
            result[name] = normalize_type(children[0], tags, atomics=True)
    return result


def scalar_spellings(tree: dict[str, Any]) -> set[str]:
    node = tree.get("node")
    if node == "scalar":
        return {tree["spelling"]}
    if node == "pointer":
        return scalar_spellings(tree["pointee"])
    if node == "array":
        return scalar_spellings(tree["element"])
    if node == "function":
        result = scalar_spellings(tree["result"])
        for parameter in tree["parameters"]:
            result |= scalar_spellings(parameter)
        return result
    return set()


def member_paths(declaration: dict[str, Any]) -> list[str]:
    """Member designators of a record: its named members and, through the IndirectFieldDecl
    nodes clang adds, the members of its anonymous struct and union members."""
    result = []
    for child in declaration.get("inner", []) or []:
        kind = child.get("kind")
        if kind == "FieldDecl":
            if child.get("isBitfield"):
                raise SurfaceError("a bit-field member is not part of the runtime surface")
            if child.get("name"):
                result.append(child["name"])
        elif kind == "IndirectFieldDecl" and child.get("name"):
            result.append(child["name"])
    return result


class Surface:
    """The table rows of the generated file, interned by structural key."""

    def __init__(self, cc: str, arguments: list[str], directory: Path, prelude: str) -> None:
        self.cc = cc
        self.arguments = arguments
        self.directory = directory
        self.prelude = prelude
        self.rows: list[dict[str, Any]] = []
        self.keys: dict[str, int] = {}
        self.fields: list[tuple[str, int, int]] = []
        self.parameters: list[int] = []
        self.scalars: dict[str, tuple[str, int, bool]] = {}
        self.pending: list[int] = []
        self.probes = 0

    def add(self, key: str, row: dict[str, Any]) -> int:
        if key not in self.keys:
            self.keys[key] = len(self.rows)
            self.rows.append(row)
        return self.keys[key]

    def learn_scalars(self, spellings: set[str]) -> None:
        unknown = sorted(spellings - set(self.scalars))
        if not unknown:
            return
        lines = []
        for index, spelling in enumerate(unknown):
            lines.append(f"enum {{ r_size_{index} = (int)sizeof({spelling}) }};")
            if spelling not in FLOATS and spelling not in ("_Bool", "bool"):
                lines.append(f"enum {{ r_signed_{index} = "
                             f"(int)(({spelling})-1 < ({spelling})0) }};")
        values = constant_values(self.probe("\n".join(lines) + "\n", with_prelude=True))
        for index, spelling in enumerate(unknown):
            size = values[f"r_size_{index}"]
            if spelling in ("_Bool", "bool"):
                self.scalars[spelling] = ("bool", size, False)
            elif spelling in FLOATS:
                self.scalars[spelling] = ("float", size, True)
            else:
                self.scalars[spelling] = ("integer", size, bool(values[f"r_signed_{index}"]))

    def probe(self, source: str, with_prelude: bool = False) -> dict[str, Any]:
        self.probes += 1
        text = (self.prelude + "\n#include <stddef.h>\n" + source) if with_prelude else source
        return ast(self.cc, self.arguments, text, self.directory, f"probe_{self.probes}.c")

    def intern(self, tree: dict[str, Any], spelling_hint: str) -> int:
        node = tree.get("node")
        const = bool(tree.get("const"))
        if tree.get("atomic"):
            plain = self.intern({key: value for key, value in tree.items() if key != "atomic"},
                                spelling_hint)
            row = dict(self.rows[plain])
            row["atomic"] = True
            return self.add(f"atomic:{plain}", row)
        if node == "void":
            return self.add("void", {"kind": "void", "size": 0, "align": 1})
        if node == "scalar":
            spelling = tree["spelling"]
            kind, size, signed = self.scalars[spelling]
            return self.add(f"scalar:{spelling}",
                            {"kind": kind, "size": size, "align": size, "signed": signed,
                             "name": spelling})
        if node == "pointer":
            target = self.intern(tree["pointee"], f"__typeof__(*(({spelling_hint})0))")
            return self.add(f"pointer:{target}:{int(const)}",
                            {"kind": "pointer", "size": 8, "align": 8, "target": target,
                             "const": const})
        if node == "array":
            element = self.intern(tree["element"], f"__typeof__((({spelling_hint} *)0)[0][0])")
            row = self.rows[element]
            return self.add(f"array:{element}:{tree['length']}",
                            {"kind": "array", "size": row["size"] * tree["length"],
                             "align": row["align"], "target": element,
                             "count": tree["length"]})
        if node == "function":
            result = self.intern(tree["result"], "")
            parameters = [self.intern(parameter, "") for parameter in tree["parameters"]]
            key = f"function:{result}:{parameters}:{int(tree['variadic'])}"
            if key in self.keys:
                return self.keys[key]
            first = len(self.parameters)
            self.parameters.extend(parameters)
            return self.add(key, {"kind": "function", "size": 0, "align": 1, "target": result,
                                  "count": len(parameters), "first": first,
                                  "variadic": bool(tree["variadic"])})
        if node in ("record", "enum"):
            declaration = tree.get("_declaration")
            tag = "enum" if node == "enum" else tree.get("tag", "struct")
            tag_name = tree.get("tag_name") or ""
            typedef = tree.get("typedef") or ""
            if tag_name:
                key, spelling, name = f"{tag}:{tag_name}", f"{tag} {tag_name}", typedef or tag_name
            elif typedef:
                key, spelling, name = f"{tag}::{typedef}", typedef, typedef
            elif spelling_hint:
                key, spelling, name = f"{tag}:{spelling_hint}", spelling_hint, ""
            else:
                raise SurfaceError("an anonymous record is reached without a spelling")
            if declaration is None:
                return self.add(f"opaque:{key}",
                                {"kind": "opaque", "size": 0, "align": 1, "name": name})
            if key in self.keys:
                row = self.rows[self.keys[key]]
                if not row.get("name") and name:
                    row["name"] = name
                return self.keys[key]
            index = self.add(key, {"kind": tag, "size": 0, "align": 1, "name": name,
                                   "_spelling": spelling, "_declaration": declaration})
            self.pending.append(index)
            return index
        raise SurfaceError(f"type {tree.get('spelling', node)} is not expressible")

    def complete(self) -> None:
        """Layout, members and enumerators of every reached record, transitively."""
        while self.pending:
            batch, self.pending = self.pending, []
            records = []
            lines = []
            for position, index in enumerate(batch):
                row = self.rows[index]
                if row["kind"] == "enum":
                    self.complete_enum(index)
                    continue
                spelling = row["_spelling"]
                members = member_paths(row["_declaration"])
                records.append((index, position, spelling, members))
                lines.append(f"enum {{ r_s_{position} = (int)sizeof({spelling}), "
                             f"r_a_{position} = (int)_Alignof({spelling}) }};")
                for member_index, path in enumerate(members):
                    lines.append(f"enum {{ r_o_{position}_{member_index} = "
                                 f"(int)offsetof({spelling}, {path}) }};")
                    lines.append(f"typedef __typeof__((({spelling} *)0)->{path}) "
                                 f"r_m_{position}_{member_index};")
            if not records:
                continue
            tree = self.probe("\n".join(lines) + "\n", with_prelude=True)
            trees = typedef_trees(tree, "r_m_")
            values = constant_values(tree)
            self.learn_scalars(set().union(*[scalar_spellings(item) for item in trees.values()])
                               if trees else set())
            for index, position, spelling, members in records:
                row = self.rows[index]
                row["size"] = values[f"r_s_{position}"]
                row["align"] = values[f"r_a_{position}"]
                fields = []
                for member_index, path in enumerate(members):
                    member_type = self.intern(trees[f"r_m_{position}_{member_index}"],
                                              f"__typeof__((({spelling} *)0)->{path})")
                    fields.append((path, values[f"r_o_{position}_{member_index}"], member_type))
                row["first"] = len(self.fields)
                row["count"] = len(fields)
                self.fields.extend(fields)

    def complete_enum(self, index: int) -> None:
        row = self.rows[index]
        names = [child["name"] for child in row["_declaration"].get("inner", []) or []
                 if child.get("kind") == "EnumConstantDecl"]
        if not names:
            row["kind"] = "opaque"
            return
        lines = [self.prelude]
        for candidate in ENUM_CANDIDATES:
            lines.append(f'_Static_assert(_Generic(({row["_spelling"]})0, {candidate}: 1, '
                         f'default: 0), "R_SURFACE {candidate}");')
        completed = compile_text(self.cc, self.arguments, "\n".join(lines) + "\n",
                                 self.directory, "enum_probe.c",
                                 ["-ferror-limit=0", "-fsyntax-only"])
        failed = set(re.findall(r'R_SURFACE ([a-z ]+)"', completed.stderr))
        survivors = [candidate for candidate in ENUM_CANDIDATES if candidate not in failed]
        if len(survivors) != 1:
            raise SurfaceError(f"enumeration {row['_spelling']}: compatible type {survivors}")
        self.learn_scalars({survivors[0]})
        underlying = self.intern({"node": "scalar", "spelling": survivors[0]}, "")
        row["target"] = underlying
        row["size"] = self.rows[underlying]["size"]
        row["align"] = self.rows[underlying]["align"]


def lowered_declarations(surface: Surface, functions: list[str]) -> dict[str, str]:
    """The declaration clang lowers each function to for the target (its C ABI)."""
    source = surface.prelude + "\nvoid (*const r_surface_table[])(void) = {\n" + "".join(
        f"    (void (*)(void))&{name},\n" for name in functions) + "};\n"
    completed = compile_text(surface.cc, surface.arguments, source, surface.directory,
                             "lowered.c", ["-O0", "-S", "-emit-llvm", "-o", "-"])
    if completed.returncode != 0:
        raise SurfaceError(f"lowering probe does not compile:\n{completed.stderr}")
    result = {}
    wanted = set(functions)
    for line in completed.stdout.splitlines():
        match = re.match(r"^declare (.*?)@([A-Za-z_][A-Za-z0-9_]*)\((.*)\)(?: #\d+)?$", line)
        if match and match.group(2) in wanted:
            result[match.group(2)] = f"{match.group(1).strip()} ({match.group(3)})"
    missing = sorted(wanted - set(result))
    if missing:
        raise SurfaceError(f"clang declared no lowering for {missing[:5]}")
    return result


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render(surface: Surface, functions: list[tuple[str, int, str]],
           typedefs: list[tuple[str, int]], constants: list[tuple[str, int, int]],
           triple: str) -> str:
    lines = [
        f"/* Generated by tools/generate_runtime_surface.py for {triple}. */",
        "",
        "static const RLlvmSurfaceType r_llvm_surface_types[] = {",
    ]
    for index, row in enumerate(surface.rows):
        flags = [flag for key, flag in (("signed", "R_LLVM_SURFACE_SIGNED"),
                                         ("const", "R_LLVM_SURFACE_CONST"),
                                         ("variadic", "R_LLVM_SURFACE_VARIADIC"),
                                         ("atomic", "R_LLVM_SURFACE_ATOMIC")) if row.get(key)]
        lines.append(
            f"    {{{KIND[row['kind']]}, {' | '.join(flags) if flags else '0U'}, "
            f"UINT32_C({row['size']}), UINT32_C({row['align']}), "
            f"UINT32_C({row.get('target', 0)}), UINT32_C({row.get('count', 0)}), "
            f"UINT32_C({row.get('first', 0)}), {c_string(row.get('name', ''))}}}, /* {index} */")
    lines += ["};", "", "static const RLlvmSurfaceField r_llvm_surface_fields[] = {"]
    for name, offset, type_index in surface.fields:
        lines.append(f"    {{{c_string(name)}, UINT32_C({offset}), UINT32_C({type_index})}},")
    lines += ["};", "", "static const uint32_t r_llvm_surface_parameters[] = {"]
    for value in surface.parameters:
        lines.append(f"    UINT32_C({value}),")
    lines += ["};", "", "/* Sorted by name. */",
              "static const RLlvmSurfaceFunction r_llvm_surface_functions[] = {"]
    for name, type_index, lowered in functions:
        lines.append(f"    {{{c_string(name)}, UINT32_C({type_index}), {c_string(lowered)}}},")
    lines += ["};", "", "/* Sorted by name. */",
              "static const RLlvmSurfaceName r_llvm_surface_typedefs[] = {"]
    for name, type_index in typedefs:
        lines.append(f"    {{{c_string(name)}, UINT32_C({type_index})}},")
    lines += ["};", "", "/* Sorted by name. */",
              "static const RLlvmSurfaceConstant r_llvm_surface_constants[] = {"]
    for name, value, type_index in constants:
        lines.append(f"    {{{c_string(name)}, INT64_C({value}), UINT32_C({type_index})}},")
    lines += ["};", ""]
    return "\n".join(lines)


def check_header_list(root: Path) -> None:
    """Every runtime or library header a test wrapper sees in place of the program
    (tests/codegen_program_prelude.h) is part of the surface, so that the shims compiled with
    the wrapper's renames cover the inline functions it can call."""
    prelude = root / "tests/codegen_program_prelude.h"
    named = set(re.findall(r'"(r_(?:std|runtime|core)[a-z0-9_]*\.h)"',
                           prelude.read_text(encoding="utf-8")))
    missing = sorted(named - set(HEADERS))
    if missing:
        raise SurfaceError(f"headers of tests/codegen_program_prelude.h are not in HEADERS: {missing}")


def inline_shims(tree: dict[str, Any], root: Path) -> list[tuple[str, str]]:
    """The prototype of the shim of every `static inline` function the headers define: its
    name and the text of its declaration without the semicolon, plus the call it makes."""
    shims = []
    for node in top_level_in_project(tree, root):
        if (node.get("kind") != "FunctionDecl" or node.get("storageClass") != "static" or
                not node.get("inline") or not any(item.get("kind") == "CompoundStmt"
                                                  for item in node.get("inner", []))):
            continue
        name = node["name"]
        whole = node["type"]["qualType"]
        result = whole[:whole.index("(")].strip()
        if "(" in result or "..." in whole:
            raise SurfaceError(f"inline function {name} has a signature a shim cannot spell")
        parameters = [item for item in node.get("inner", []) if item.get("kind") == "ParmVarDecl"]
        spelled = []
        for index, parameter in enumerate(parameters):
            text = parameter["type"]["qualType"]
            argument = f"a{index}"
            spelled.append(text.replace("(*)", f"(*{argument})", 1) if "(*)" in text
                           else f"{text} {argument}")
        prototype = f"{result} {SHIM_PREFIX}{name}({', '.join(spelled) or 'void'})"
        call = f"{name}({', '.join(f'a{index}' for index in range(len(parameters)))})"
        body = f"{call};" if result == "void" else f"return {call};"
        shims.append((prototype, body))
    return sorted(shims, key=lambda item: item[0].split("(")[0].split()[-1])


def render_shims(prelude: str, shims: list[tuple[str, str]]) -> str:
    lines = ["/* Generated by tools/generate_runtime_surface.py; do not edit. */",
             "/* The static inline functions of the runtime and library headers as external",
             "   functions the LLVM backend can call (B4 of the LLVM transition). */", ""]
    lines += prelude.rstrip("\n").split("\n")
    lines.append("")
    for prototype, _ in shims:
        lines.append(f"{prototype};")
    for prototype, body in shims:
        lines += ["", f"{prototype} {{", f"    {body}", "}"]
    return "\n".join(lines) + "\n"


def generate(root: Path, manifest: dict[str, Any], cc: str, sysroot: str | None) -> tuple[str, str]:
    check_header_list(root)
    triple = manifest["toolchain"]["llvm_triple"]
    arguments = ["-std=c17", "-target", triple]
    if sysroot:
        arguments += ["-isysroot", sysroot]
    for include in INCLUDE_ROOTS:
        arguments.append(f"-I{root / include}")
    for include in sorted(glob.glob(str(root / "library/std/*/include"))):
        arguments.append(f"-I{include}")
    prelude = "".join(f'#include "{header}"\n' for header in HEADERS)
    with tempfile.TemporaryDirectory(prefix="r-runtime-surface-") as scratch:
        tree = Surface(cc, arguments, Path(scratch), prelude).probe(prelude)
        shims = inline_shims(tree, root)
        shim_text = render_shims(prelude, shims)
        prelude += "".join(f"{prototype};\n" for prototype, _ in shims)
        surface = Surface(cc, arguments, Path(scratch), prelude)
        tree = surface.probe(prelude)
        function_names = sorted({node["name"] for node in top_level_in_project(tree, root)
                                 if node.get("kind") == "FunctionDecl" and
                                 node.get("storageClass") != "static"} |
                                {prototype.split("(")[0].split()[-1] for prototype, _ in shims})
        typedef_names = sorted({node["name"] for node in top_level_in_project(tree, root)
                                if node.get("kind") == "TypedefDecl"})
        enumerator_names = sorted({node["name"] for top in top_level_in_project(tree, root)
                                   for node in walk(top)
                                   if node.get("kind") == "EnumConstantDecl"})
        # An enumerator without an initializer carries no value in the AST; a probe evaluates
        # every one (an enumeration constant has type int in C17).
        values = constant_values(surface.probe("".join(
            f"enum {{ r_v_{index} = {name} }};\n" for index, name in enumerate(enumerator_names)),
            with_prelude=True))
        enumerators = [(name, values[f"r_v_{index}"])
                       for index, name in enumerate(enumerator_names)]
        lines = []
        for index, name in enumerate(function_names):
            lines.append(f"typedef __typeof__({name}) r_f_{index};")
        for index, name in enumerate(typedef_names):
            lines.append(f"typedef {name} r_t_{index};")
        for index, (name, _) in enumerate(enumerators):
            lines.append(f"typedef __typeof__({name}) r_e_{index};")
        probe = surface.probe("\n".join(lines) + "\n", with_prelude=True)
        function_trees = typedef_trees(probe, "r_f_")
        named_trees = typedef_trees(probe, "r_t_")
        enumerator_trees = typedef_trees(probe, "r_e_")
        everything = list(function_trees.values()) + list(named_trees.values())
        surface.learn_scalars(set().union(*[scalar_spellings(item) for item in everything]) |
                              set().union(*[scalar_spellings(item) for item in
                                            enumerator_trees.values()]))
        typedefs = []
        for index, name in enumerate(typedef_names):
            typedefs.append((name, surface.intern(named_trees[f"r_t_{index}"], name)))
        functions = []
        for index, name in enumerate(function_names):
            functions.append((name, surface.intern(function_trees[f"r_f_{index}"], "")))
        constants = []
        for index, (name, value) in enumerate(enumerators):
            constants.append((name, value,
                              surface.intern(enumerator_trees[f"r_e_{index}"], "")))
        surface.complete()
        lowered = lowered_declarations(surface, function_names)
    return render(surface,
                  [(name, type_index, lowered[name]) for name, type_index in functions],
                  sorted(typedefs), sorted(constants), triple), shim_text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--sysroot")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--verify", action="store_true")
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    try:
        manifest = json.loads(arguments.manifest.read_text(encoding="utf-8"))
        texts = dict(zip((OUTPUT, SHIMS), generate(root, manifest, arguments.cc, arguments.sysroot)))
    except (OSError, SurfaceError, KeyError, json.JSONDecodeError) as error:
        print(f"runtime surface: {error}", file=sys.stderr)
        return 1
    stale = False
    for relative, text in texts.items():
        path = root / relative
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if arguments.write:
            if current != text:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8", newline="\n")
        elif current != text:
            print(f"runtime surface: {relative} is stale; regenerate it with --write", file=sys.stderr)
            stale = True
    return 1 if stale else 0


if __name__ == "__main__":
    raise SystemExit(main())
