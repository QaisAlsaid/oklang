from pathlib import Path


out = Path(__file__).resolve().parent.parent / "tests" / "generated"
out.mkdir(parents=True, exist_ok=True)


def write(name, lines):
  (out / name).write_text("\n".join(lines) + "\n")

# constant long

lines = [
  "// expect: 255",
  "// expect: 256",
  "if false? {",
]

for i in range(257):
  lines.append(f"{i};")

lines += [
  "}",
  "print(255);",
  "print(256);",
]

write("constant_long.ok", lines)

# global long

lines = []

for i in range(257):
  lines.append(f"glob let mut g{i} = {i};")

lines += [
  "// expect: 256",
  "print(g256);",
  "g256 = 999;",
  "// expect: 999",
  "print(g256);",
]

write("global_long.ok", lines)


# Local long

lines = []

for i in range(257):
  lines.append(f"let mut local{i} = {i};")

lines += [
  "// expect: 256",
  "print(local256);",
  "local256 = 999;",
  "// expect: 999",
  "print(local256);",
]

write("local_long.ok", lines)


# Upvalue long

lines = []

for i in range(257):
  lines.append(f"let mut x{i} = {i};")

lines += [
  "let f = fu () {",
]

for i in range(257):
  lines.append(f"x{i};")

lines += [
  "x256 = 999;",
  "// expect: 999",
  "print x256;",
  "};",
  "f();",
]

write("upvalue_long.ok", lines)


# Long conditional jump

lines = [
  "// expect: ok",
  "if false? {",
]

for _ in range(300):
  lines.append("0;")

lines += [
  "} else? print \"ok\";",
]

write("jump_long.ok", lines)
