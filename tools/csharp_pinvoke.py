#!/usr/bin/env python3
"""Read P/Invoke declarations from decompiled C# sources."""

import re
import sys
from pathlib import Path


DLL_IMPORT = re.compile(
    r"\[(?:global::)?(?:System\.Runtime\.InteropServices\.)?DllImport(?:Attribute)?\s*\((.*?)\)\]",
    re.DOTALL,
)
ENTRY_POINT = re.compile(r'\bEntryPoint\s*=\s*"([^"]+)"')

CPP_TYPES = {
    "void": "void", "ptr": "void *", "fnptr": "void *", "str": "const char *",
    "i1": "int8_t", "i2": "int16_t", "i4": "int32_t", "i8": "int64_t",
    "u1": "uint8_t", "u2": "uint16_t", "u4": "uint32_t", "u8": "uint64_t",
    "r4": "float", "r8": "double", "bool1": "uint8_t", "bool4": "int32_t",
}
CPP_RESERVED = {
    "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch",
    "char", "class", "const", "constexpr", "continue", "default", "delete", "do",
    "double", "else", "enum", "explicit", "extern", "false", "float", "for", "friend",
    "goto", "if", "inline", "int", "long", "namespace", "new", "noexcept", "not",
    "nullptr", "operator", "or", "private", "protected", "public", "register", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "template", "this", "throw",
    "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual", "void",
    "volatile", "while",
}


def normalize_type(value):
    return re.sub(r"\s+", "", value.replace("global::", "")).rstrip("?")


def cpp_identifier(name):
    name = re.sub(r"[^A-Za-z0-9_]", "_", name).strip("_")
    return name if name and not name[0].isdigit() else f"function_{name}"


def parameter_names(params):
    names = []
    for index, param in enumerate(params):
        name = cpp_identifier(param.get("n", f"arg{index}").lstrip("_"))
        names.append(f"{name}_value" if name in CPP_RESERVED else name)
    return names


def split_list(value):
    parts = []
    start = 0
    depths = {"(": 0, "[": 0, "<": 0}
    pairs = {")": "(", "]": "[", ">": "<"}
    quote = None
    escape = False
    for index, char in enumerate(value):
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = None
            continue
        if char in {'"', "'"}:
            quote = char
        elif char in depths:
            depths[char] += 1
        elif char in pairs:
            depths[pairs[char]] -= 1
        elif char == "," and not any(depths.values()):
            parts.append(value[start:index].strip())
            start = index + 1
    tail = value[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def strip_attributes(value):
    value = value.strip()
    while value.startswith("["):
        depth = 0
        quote = None
        for index, char in enumerate(value):
            if quote:
                if char == quote and value[index - 1:index] != "\\":
                    quote = None
            elif char in {'"', "'"}:
                quote = char
            elif char == "[":
                depth += 1
            elif char == "]":
                depth -= 1
                if depth == 0:
                    value = value[index + 1:].strip()
                    break
        else:
            raise ValueError(f"unterminated C# attribute: {value}")
    return value


def parse_parameter(value):
    value = strip_attributes(value)
    match = re.fullmatch(r"(?:(ref|out|in|params)\s+)?(.+?)\s+(@?[A-Za-z_]\w*)", value)
    if not match:
        raise ValueError(f"unsupported C# parameter: {value}")
    return {"modifier": match.group(1), "type": match.group(2).strip(),
            "name": match.group(3).lstrip("@")}


def parse_sources(source_root, native_dll):
    declarations = []
    for path in sorted(Path(source_root).rglob("*.cs")):
        text = path.read_text(encoding="utf-8")
        if native_dll not in text:
            continue
        for match in DLL_IMPORT.finditer(text):
            arguments = match.group(1)
            library = re.search(r'"([^"]+)"', arguments)
            if not library or library.group(1) != native_dll:
                continue
            end = text.find(";", match.end())
            if end < 0:
                raise ValueError(f"unterminated P/Invoke after {path}:{text.count(chr(10), 0, match.start()) + 1}")
            declaration = text[match.end():end + 1]
            method = re.search(
                r"\bextern\s+(.+?)\s+(@?[A-Za-z_]\w*)\s*\((.*)\)\s*;",
                declaration, re.DOTALL)
            if not method:
                raise ValueError(f"unsupported P/Invoke declaration in {path}: {declaration.strip()}")
            parameters = split_list(method.group(3)) if method.group(3).strip() else []
            entry_point = ENTRY_POINT.search(arguments)
            declarations.append({
                "entry_point": entry_point.group(1) if entry_point else method.group(2).lstrip("@"),
                "name": method.group(2).lstrip("@"),
                "ret": method.group(1).strip(),
                "return_i1": "return:" in declaration and "UnmanagedType.I1" in declaration,
                "params": [parse_parameter(parameter) for parameter in parameters],
            })
    if not declarations:
        raise SystemExit(f"no {native_dll} DllImport declarations found under {source_root}")
    return declarations


def load_decompiled_sources(input_path, assembly, native_dll):
    input_path = Path(input_path).expanduser().resolve()
    candidates = (input_path / assembly, input_path / "src" / assembly, input_path)
    for candidate in candidates:
        if candidate.is_dir():
            try:
                return parse_sources(candidate, native_dll)
            except SystemExit:
                continue
    raise SystemExit(f"expected decompiled {assembly} sources under {input_path}")


def load_generator_declarations(assembly, native_dll):
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} DECOMPILED_ROOT")
    return load_decompiled_sources(sys.argv[1], assembly, native_dll)
