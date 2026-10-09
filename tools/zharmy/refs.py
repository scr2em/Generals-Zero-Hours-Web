"""Finding and rewriting the references inside definitions (uses ``schema``)."""

from . import ini as inimod
from . import schema

FIRST_TOKEN_ONLY = frozenset(["Label", "Texture", "TrackFile", "SpeechFile", "Anim"])
SKIP_TOKENS = frozenset(["none", "nosound", "(none)"])
# the veterancy / weapon slot / locomotor set keywords that share a field with names
KEYWORDS = frozenset(["primary", "secondary", "tertiary", "regular", "veteran", "elite", "heroic", "all",
                      "initial_death", "final_death"])


class Ref:
    """One token of a field that names something."""

    __slots__ = ("field", "chain", "index", "kinds", "token")

    def __init__(self, field, chain, index, kinds, token):
        self.field, self.chain, self.index, self.kinds, self.token = field, chain, index, kinds, token


def field_refs(top):
    """Every reference in a top level node, as a list of ``Ref`` (one per token and field)."""
    out = []
    for field, chain in inimod.walk_fields(top):
        if not field.args:
            continue
        kinds = schema.kinds_for(schema.scopes_for(chain), field.name)
        if not kinds:
            continue
        tokens = field.args.split()
        for i, tok in enumerate(tokens):
            if tok.startswith('"'):
                continue
            if tok.lower() in SKIP_TOKENS:
                continue
            ks = tuple(k for k in kinds if not (k in FIRST_TOKEN_ONLY and i > 0))
            if not ks:
                continue
            out.append(Ref(field, chain, i, ks, tok))
    return out


def header_refs(top):
    """References held in block headers: the parent of an ObjectReskin."""
    out = []
    if top.name == "ObjectReskin":
        toks = top.args.split()
        if len(toks) >= 2:
            out.append(("Object", toks[1]))
    return out


def rewrite_fields(top, mapper):
    """Apply ``mapper(kind, token) -> new token or None`` to every reference token of ``top`` in place.

    Returns the number of tokens changed.
    """
    changed = 0
    by_field = {}
    for r in field_refs(top):
        by_field.setdefault(id(r.field), (r.field, []))[1].append(r)
    for field, refs in by_field.values():
        tokens = field.args.split()
        dirty = False
        for r in refs:
            for kind in r.kinds:
                new = mapper(kind, r.token)
                if new is not None and new != r.token:
                    tokens[r.index] = new
                    dirty = True
                    changed += 1
                    break
        if dirty:
            field.args = " ".join(tokens)
    return changed


def unknown_fields(top):
    """Field names of a definition that no engine table knows (reported, kept)."""
    out = []
    for field, chain in inimod.walk_fields(top):
        if not schema.is_known_field(field.name):
            out.append(field.name)
    return out


def module_headers(top):
    """Yield (parent_node, header_node, module_name) for every module block below ``top``."""
    def rec(node):
        for c in node.children or ():
            if c.is_block:
                if c.name in inimod.MODULE_HEADERS:
                    toks = c.args.split()
                    yield node, c, (toks[0] if toks else "")
                yield from rec(c)
    yield from rec(top)


def strip_unknown_modules(top):
    """Remove module blocks the engine does not know. Returns their names."""
    removed = []
    for parent, header, name in list(module_headers(top)):
        if name and not schema.is_known_module(name):
            parent.children = [c for c in parent.children if c is not header]
            removed.append(name)
    return removed


def is_keyword(token):
    t = token.lower()
    return t in KEYWORDS or t.startswith("set_")


def strict_ref(ref):
    """True when the field holds exactly one name of one definition kind (keywords aside): it must resolve."""
    if len(ref.kinds) != 1 or ref.kinds[0] not in schema.DEFINITION_KINDS:
        return False
    return len([t for t in ref.field.args.split() if not is_keyword(t)]) == 1
