"""Extract INI field tables from the engine's C++ sources.

The pack never copies game data, but it must use the exact field names the
engine's parsers accept. This module reads the repository's own ``FieldParse``
tables (``{ "FieldName", INI::parseXxx, userData, offsetof(...) }``) and module
``buildFieldParse`` chains, so the validator (``validate_ini.py``) can reject any
field that the real parser would not know, and the content author can look up the
fields of any block or module (``python -m spk.engine_schema Weapon``).

Only text processing; nothing is compiled.
"""

import os
import re
import sys

SOURCE_DIRS = ["Core/GameEngine", "GeneralsMD/Code/GameEngine", "Core/GameEngineDevice",
               "GeneralsMD/Code/GameEngineDevice", "Core/Libraries/Source/WWVegas/WW3D2"]

FIELD_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*([A-Za-z_][\w:]*)\s*,\s*(.*?)\s*,\s*(?:offsetof|0|\(|[A-Za-z_])', re.S)
ARRAY_RE = re.compile(r'FieldParse\s+((?:\w+::)*\w+)\s*\[\s*\w*\s*\]\s*=\s*\{(.*?)\n\s*\}\s*;', re.S)
NAMES_RE = re.compile(r'(?:const\s+)?char\s*\*\s*(?:const\s+)?((?:\w+::)*\w+)\s*\[\s*\w*\s*\]\s*=\s*\{(.*?)\}\s*;', re.S)
LOOKUP_RE = re.compile(r'LookupListRec\s+((?:\w+::)*\w+)\s*\[\s*\w*\s*\]\s*=\s*\{(.*?)\}\s*;', re.S)
BUILD_RE = re.compile(r'void\s+(\w+)::buildFieldParse\s*\(\s*MultiIniFieldParse\s*&\s*\w+\s*\)\s*\{', re.S)
INLINE_BUILD_RE = re.compile(r'static\s+void\s+buildFieldParse\s*\(\s*MultiIniFieldParse\s*&\s*\w+\s*\)\s*\{', re.S)
CLASS_OPEN_RE = re.compile(r'\b(?:class|struct)\s+(?:\w+\s+)?(\w+)\s*(?::[^{;]*)?\{')
GETFP_RE = re.compile(r'(?:const\s+)?FieldParse\s*\*\s*(\w+)::getFieldParse\s*\(\s*\)\s*(?:const\s*)?\{', re.S)
ADDMODULE_RE = re.compile(r'\baddModule\s*\(\s*(\w+)\s*\)')
MACRO_RE = re.compile(r'MAKE_STANDARD_MODULE_(?:DATA_)?MACRO(?:_WITH_MODULE_DATA|_ABC)?\s*\(\s*(\w+)\s*(?:,\s*(\w+)\s*)?\)')
CLASS_RE = re.compile(r'\bclass\s+(?:CBP_EXPORT\s+)?(\w+)\s*:\s*public\s+(\w+)')


def _strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def _balanced(text, open_pos):
    """Return the index just after the brace matching the '{' at open_pos."""
    depth = 0
    i = open_pos
    while i < len(text):
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == '"':
            i += 1
            while text[i] != '"':
                if text[i] == '\\':
                    i += 1
                i += 1
        i += 1
    raise ValueError("unbalanced braces")


class Field:
    def __init__(self, token, parser, userdata, source):
        self.token = token
        self.parser = parser.split("::")[-1]
        self.userdata = userdata.strip()
        self.source = source

    def __repr__(self):
        return "Field(%s, %s, %s)" % (self.token, self.parser, self.userdata)


class Schema:
    """All field tables found in the engine sources."""

    def __init__(self, repo_root):
        self.root = repo_root
        self.tables = {}        # short name -> [list of Field] (first definition wins; see all_tables)
        self.all_tables = {}    # short name -> list of tables
        self.name_lists = {}    # identifier -> [names]
        self.lookup_lists = {}  # identifier -> [names]
        self.build_functions = {}  # class -> (tables [names or inline lists], parent)
        self.get_field_parse = {}
        self.module_names = set()
        self.module_data_class = {}  # module -> module data class
        self.class_parent = {}
        self._load()

    # ---- loading --------------------------------------------------------------
    def _files(self):
        for d in SOURCE_DIRS:
            base = os.path.join(self.root, d)
            for dirpath, _dirs, files in os.walk(base):
                for f in files:
                    if f.endswith((".cpp", ".h", ".inl")):
                        yield os.path.join(dirpath, f)

    def _load(self):
        for path in sorted(self._files()):
            try:
                text = _strip_comments(open(path, encoding="latin-1").read())
            except OSError:
                continue
            rel = os.path.relpath(path, self.root)
            for m in ARRAY_RE.finditer(text):
                name = m.group(1).split("::")[-1]
                fields = self._parse_fields(m.group(2), rel)
                self.all_tables.setdefault(name, []).append(fields)
                self.tables.setdefault(name, fields)
            for m in NAMES_RE.finditer(text):
                names = re.findall(r'"([^"]*)"', m.group(2))
                if names:
                    self.name_lists.setdefault(m.group(1), names)
                    self.name_lists.setdefault(m.group(1).split("::")[-1], names)
            for m in LOOKUP_RE.finditer(text):
                names = re.findall(r'\{\s*"([^"]*)"', m.group(2))
                if names:
                    self.lookup_lists.setdefault(m.group(1).split("::")[-1], names)
            builds = [(m.group(1), m.end() - 1) for m in BUILD_RE.finditer(text)]
            for m in INLINE_BUILD_RE.finditer(text):
                owners = [c for c in CLASS_OPEN_RE.finditer(text, 0, m.start())]
                if owners:
                    builds.append((owners[-1].group(1), m.end() - 1))
            for cls, brace in builds:
                end = _balanced(text, brace)
                body = text[brace + 1:end]
                parent = None
                for pm in re.finditer(r'(\w+)::buildFieldParse\s*\(\s*\w+\s*\)', body):
                    if pm.group(1) != cls:
                        parent = pm.group(1)
                        break
                tables = []
                for am in ARRAY_RE.finditer(body):
                    tables.append(self._parse_fields(am.group(2), rel))
                for ref in re.findall(r'\.add\s*\(\s*(\w+)', body):
                    if ref in self.tables and not any(t is self.tables[ref] for t in tables):
                        tables.append(self.tables[ref])
                for ref in re.findall(r'\.add\s*\(\s*(\w+)::getFieldParse\s*\(\s*\)', body):
                    tables.append(("lazy", ref))
                self.build_functions[cls] = (tables, parent)
            for m in GETFP_RE.finditer(text):
                cls = m.group(1)
                end = _balanced(text, m.end() - 1)
                body = text[m.end():end]
                tabs = [self._parse_fields(am.group(2), rel) for am in ARRAY_RE.finditer(body)]
                if tabs:
                    self.get_field_parse.setdefault(cls, tabs[0])
            for m in ADDMODULE_RE.finditer(text):
                self.module_names.add(m.group(1))
            for m in MACRO_RE.finditer(text):
                if m.group(2):
                    self.module_data_class.setdefault(m.group(1), m.group(2))
            for m in CLASS_RE.finditer(text):
                self.class_parent.setdefault(m.group(1), m.group(2))

    @staticmethod
    def _parse_fields(body, source):
        fields = []
        for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*([A-Za-z_][\w:]*)\s*,\s*((?:\([^)]*\)\s*)?[\w:&\[\]\.>\-]*)\s*,', body):
            fields.append(Field(m.group(1), m.group(2), m.group(3), source))
        return fields

    # ---- queries ----------------------------------------------------------------
    def table(self, name):
        """Fields of the table with this identifier, or ``None``."""
        t = self.tables.get(name)
        return t

    def module_fields(self, module):
        """All fields a module block accepts (own data class, parents), name -> Field."""
        data_cls = self._module_data_class(module)
        result = {}
        seen = set()
        cls = data_cls
        while cls and cls not in seen:
            seen.add(cls)
            tables, parent = self.build_functions.get(cls, ([], None))
            for t in tables:
                if isinstance(t, tuple):
                    t = self.get_field_parse.get(t[1], [])
                for f in t:
                    result.setdefault(f.token, f)
            cls = parent
        return result

    def _module_data_class(self, module):
        cls = module
        seen = set()
        while cls and cls not in seen:
            seen.add(cls)
            if cls in self.module_data_class:
                return self.module_data_class[cls]
            cls = self.class_parent.get(cls)
        return module + "ModuleData"

    def is_module(self, name):
        return name in self.module_names


def find_repo_root(start=None):
    d = os.path.abspath(start or os.path.dirname(__file__))
    while d != os.path.dirname(d):
        if os.path.isdir(os.path.join(d, "GeneralsMD")) and os.path.isdir(os.path.join(d, "Core")):
            return d
        d = os.path.dirname(d)
    raise RuntimeError("repository root not found")


def main(argv):
    schema = Schema(find_repo_root())
    if len(argv) < 2:
        print("usage: engine_schema.py <TableName|ModuleName> ...")
        return 1
    for name in argv[1:]:
        if schema.is_module(name):
            fields = schema.module_fields(name)
            print("module %s (%d fields)" % (name, len(fields)))
        elif name in schema.tables:
            fields = {f.token: f for f in schema.tables[name]}
            print("table %s (%d fields)" % (name, len(fields)))
        else:
            print("%s: not found" % name)
            continue
        for f in fields.values():
            extra = ("  [%s]" % f.userdata) if f.userdata not in ("nullptr", "0", "NULL", "") else ""
            print("  %-34s %s%s" % (f.token, f.parser, extra))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
